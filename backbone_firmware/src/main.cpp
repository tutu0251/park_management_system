// =============================================================================
// main.cpp — park payment BACKBONE (gateway): relay node RF <-> server RF
// =============================================================================
// RX pipe 1: frames from park nodes (swipes, status replies, proactive status).
// RX pipe 2: frames from server (e.g. CHECK_STATUS_REQ broadcast trigger).
// TX: switch writing pipe per destination (server or specific node address).
// =============================================================================

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

#include "backbone_config.h"  // cfg:: pins and ADDR_* arrays
#include "backbone_nodes.h"   // KNOWN_NODES, find_node_by_id
#include "node_protocol.h"    // Shared proto structs and parse_rx

namespace {

#ifndef RADIO_FAIL_THRESHOLD
#define RADIO_FAIL_THRESHOLD 8   // Server-direction TX failures before log spam.
#endif

#ifndef STATUS_POLL_MS
#define STATUS_POLL_MS 30000       // Interval between proactive status polls.
#endif

#ifndef SERVER_REPLY_TIMEOUT_MS
#define SERVER_REPLY_TIMEOUT_MS 1200   // PAY_RESP wait after forwarding swipe.
#endif

RF24 radio(cfg::RF_CE_PIN, cfg::RF_CSN_PIN);

uint8_t g_fail_streak = 0;   // Consecutive failures sending toward server address.

// Initialize RF24: dual read pipes for node vs server, default TX to server.
bool radio_begin() {
  if (!radio.begin()) return false;

  radio.setChannel(cfg::RF_CHANNEL);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(32);    // Fixed width matches proto::PAYLOAD_MAX.
  radio.setAutoAck(true);

  // Pipe 1: uplink from any node that targets ADDR_GW_NODE.
  radio.openReadingPipe(1, cfg::ADDR_GW_NODE);
  // Pipe 2: downlink from server peer listening on ADDR_GW_SERVER.
  radio.openReadingPipe(2, cfg::ADDR_GW_SERVER);

  // After TX, return to listening; default write target is server.
  radio.openWritingPipe(cfg::ADDR_SERVER);
  radio.startListening();
  return true;
}

// Generic TX helper: copies len bytes into zero-padded 32-byte ShockBurst packet.
bool radio_send_to(const uint8_t addr[5], const uint8_t* payload, size_t len) {
  if (!addr || !payload || len == 0 || len > 32) return false;

  uint8_t frame[32];
  memset(frame, 0, sizeof(frame));
  memcpy(frame, payload, len);

  radio.stopListening();
  radio.openWritingPipe(addr);              // RF24: one open writing pipe at a time.
  const bool ok = radio.write(frame, sizeof(frame));  // Always send full 32 bytes.
  radio.startListening();
  return ok;
}

// Non-blocking read: if a packet is waiting, read 32 bytes and report which pipe.
bool radio_try_receive(uint8_t* out32, uint8_t* pipe_out) {
  if (!out32) return false;
  if (!radio.available(pipe_out)) return false;
  radio.read(out32, 32);
  return true;
}

// Construct PAY_ERROR and send to node's listening address (comfortable failure).
void send_pay_error_to_node(const NodeEntry& node) {
  proto::PayRespPacked p{};
  p.type = proto::PAY_RESP;
  p.result = proto::PAY_ERROR;
  p.credit_after_cents = 0;
  radio_send_to(node.addr, reinterpret_cast<const uint8_t*>(&p), sizeof(p));
}

// Block until a packet arrives on pipe 2 (server) or timeout (millis-based).
bool wait_server_reply(uint8_t* out32, uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  uint8_t pipe = 0;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    if (!radio_try_receive(out32, &pipe)) continue;
    if (pipe != 2) continue;   // Ignore node-originated frames during this wait.
    return true;
  }
  return false;
}

// Handle one 32-byte frame received from a node (pipe 1 in loop()).
void handle_from_node(const uint8_t* frame32) {
  if (!frame32) return;

  // Fast filter: only types the gateway understands for forwarding logic.
  if (frame32[0] != proto::SWIPE_REQ && frame32[0] != proto::STATUS_RESP &&
      frame32[0] != proto::STATUS_PUSH) {
    return;
  }

  uint16_t node_id = 0;
  if (frame32[0] == proto::SWIPE_REQ && sizeof(proto::SwipeReqPacked) <= 32) {
    proto::SwipeReqPacked s{};
    memcpy(&s, frame32, sizeof(s));
    node_id = s.node_id;
    Serial.print(F("rx swipe node="));
    Serial.print(node_id);
    Serial.print(F(" reader="));
    Serial.print(s.reader_id);
    Serial.print(F(" txn="));
    Serial.println(s.transaction_id);
  }

  const NodeEntry* node = node_id ? find_node_by_id(node_id) : nullptr;
  if (frame32[0] == proto::SWIPE_REQ && !node) {
    Serial.println(F("unknown node_id; drop swipe"));
    return;
  }

  // Uplink copy toward billing server (always full 32 bytes like radio_send_to).
  if (!radio_send_to(cfg::ADDR_SERVER, frame32, 32)) {
    Serial.println(F("tx->server failed"));
    if (++g_fail_streak >= RADIO_FAIL_THRESHOLD) Serial.println(F("radio_error (server)"));
    if (node) send_pay_error_to_node(*node);   // Do not leave node hanging on swipe.
    return;
  }
  g_fail_streak = 0;

  // Swipes need synchronous PAY_RESP relay; status frames are fire-and-forget uplink.
  if (frame32[0] == proto::SWIPE_REQ && node) {
    uint8_t reply[32];
    if (!wait_server_reply(reply, SERVER_REPLY_TIMEOUT_MS)) {
      Serial.println(F("server timeout"));
      send_pay_error_to_node(*node);
      return;
    }

    proto::PayResult r = proto::PAY_ERROR;
    uint32_t credit = 0;
    bool chk = false;
    if (!proto::parse_rx(reply, 32, &r, &credit, &chk) || reply[0] != proto::PAY_RESP) {
      Serial.println(F("bad server reply"));
      send_pay_error_to_node(*node);
      return;
    }

    Serial.print(F("tx pay_resp->node "));
    Serial.println(proto::pay_result_name(r));
    radio_send_to(node->addr, reply, 32);   // Full frame preserves any future fields.
  }
}

// Periodically ask each known node for STATUS_RESP and forward first answer to server.
void poll_all_nodes_status() {
  static uint32_t next_ms = 0;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_ms) < 0) return;   // Throttle to STATUS_POLL_MS.
  next_ms = now + STATUS_POLL_MS;

  uint8_t req[1] = {proto::CHECK_STATUS_REQ};   // Single-byte poll command.

  for (size_t i = 0; i < (sizeof(KNOWN_NODES) / sizeof(KNOWN_NODES[0])); ++i) {
    const NodeEntry& node = KNOWN_NODES[i];

    Serial.print(F("check_status node="));
    Serial.println(node.node_id);

    radio_send_to(node.addr, req, sizeof(req));   // TX to that node's listen addr.

    // Short listen window: first valid status frame wins for this poll cycle.
    const uint32_t deadline = millis() + 250;
    uint8_t frame[32];
    uint8_t pipe = 0;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
      if (!radio_try_receive(frame, &pipe)) continue;
      if (pipe != 1) continue;   // Must be from node pipe, not server chatter.
      if (frame[0] != proto::STATUS_RESP && frame[0] != proto::STATUS_PUSH) continue;
      radio_send_to(cfg::ADDR_SERVER, frame, 32);
      break;
    }
  }
}

// Server requested status check: fan out CHECK_STATUS_REQ to every known node.
void forward_server_checks_to_nodes(const uint8_t* frame32) {
  if (!frame32) return;
  if (frame32[0] != proto::CHECK_STATUS_REQ) return;

  uint8_t req[1] = {proto::CHECK_STATUS_REQ};
  for (size_t i = 0; i < (sizeof(KNOWN_NODES) / sizeof(KNOWN_NODES[0])); ++i) {
    radio_send_to(KNOWN_NODES[i].addr, req, sizeof(req));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(10);

  if (!radio_begin()) {
    Serial.println(F("radio begin failed"));
  } else {
    Serial.println(F("backbone_ok"));
  }
}

void loop() {
  uint8_t frame[32];
  uint8_t pipe = 0;

  // Drain all pending RX packets this iteration to avoid backlog growth.
  while (radio_try_receive(frame, &pipe)) {
    if (pipe == 1) {
      handle_from_node(frame);
    } else if (pipe == 2) {
      forward_server_checks_to_nodes(frame);
    }
  }

  poll_all_nodes_status();
}
