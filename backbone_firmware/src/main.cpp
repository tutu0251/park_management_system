#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

#include "backbone_config.h"
#include "backbone_nodes.h"
#include "node_protocol.h"

namespace {

#ifndef RADIO_FAIL_THRESHOLD
#define RADIO_FAIL_THRESHOLD 8
#endif

#ifndef STATUS_POLL_MS
#define STATUS_POLL_MS 30000
#endif

#ifndef SERVER_REPLY_TIMEOUT_MS
#define SERVER_REPLY_TIMEOUT_MS 1200
#endif

RF24 radio(cfg::RF_CE_PIN, cfg::RF_CSN_PIN);

uint8_t g_fail_streak = 0;

bool radio_begin() {
  if (!radio.begin()) return false;

  radio.setChannel(cfg::RF_CHANNEL);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(32);
  radio.setAutoAck(true);

  // Pipe 1: nodes -> gateway
  radio.openReadingPipe(1, cfg::ADDR_GW_NODE);
  // Pipe 2: server -> gateway (separate address so we can tell traffic apart)
  radio.openReadingPipe(2, cfg::ADDR_GW_SERVER);

  // Default writing pipe: server
  radio.openWritingPipe(cfg::ADDR_SERVER);
  radio.startListening();
  return true;
}

bool radio_send_to(const uint8_t addr[5], const uint8_t* payload, size_t len) {
  if (!addr || !payload || len == 0 || len > 32) return false;

  uint8_t frame[32];
  memset(frame, 0, sizeof(frame));
  memcpy(frame, payload, len);

  radio.stopListening();
  radio.openWritingPipe(addr);
  const bool ok = radio.write(frame, sizeof(frame));
  radio.startListening();
  return ok;
}

bool radio_try_receive(uint8_t* out32, uint8_t* pipe_out) {
  if (!out32) return false;
  if (!radio.available(pipe_out)) return false;
  radio.read(out32, 32);
  return true;
}

void send_pay_error_to_node(const NodeEntry& node) {
  proto::PayRespPacked p{};
  p.type = proto::PAY_RESP;
  p.result = proto::PAY_ERROR;
  p.credit_after_cents = 0;
  radio_send_to(node.addr, reinterpret_cast<const uint8_t*>(&p), sizeof(p));
}

bool wait_server_reply(uint8_t* out32, uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  uint8_t pipe = 0;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    if (!radio_try_receive(out32, &pipe)) continue;
    if (pipe != 2) continue;  // only accept from server pipe
    return true;
  }
  return false;
}

void handle_from_node(const uint8_t* frame32) {
  if (!frame32) return;

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

  // Forward to server.
  if (!radio_send_to(cfg::ADDR_SERVER, frame32, 32)) {
    Serial.println(F("tx->server failed"));
    if (++g_fail_streak >= RADIO_FAIL_THRESHOLD) Serial.println(F("radio_error (server)"));
    if (node) send_pay_error_to_node(*node);
    return;
  }
  g_fail_streak = 0;

  // For swipes, synchronously wait for the server's PAY_RESP and relay to the node.
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
    radio_send_to(node->addr, reply, 32);
  }
}

void poll_all_nodes_status() {
  static uint32_t next_ms = 0;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_ms) < 0) return;
  next_ms = now + STATUS_POLL_MS;

  uint8_t req[1] = {proto::CHECK_STATUS_REQ};

  for (size_t i = 0; i < (sizeof(KNOWN_NODES) / sizeof(KNOWN_NODES[0])); ++i) {
    const NodeEntry& node = KNOWN_NODES[i];

    Serial.print(F("check_status node="));
    Serial.println(node.node_id);

    // Ask node for status.
    radio_send_to(node.addr, req, sizeof(req));

    // Wait briefly for STATUS_RESP from that node, then forward to server.
    const uint32_t deadline = millis() + 250;
    uint8_t frame[32];
    uint8_t pipe = 0;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
      if (!radio_try_receive(frame, &pipe)) continue;
      if (pipe != 1) continue;  // node pipe
      if (frame[0] != proto::STATUS_RESP && frame[0] != proto::STATUS_PUSH) continue;
      radio_send_to(cfg::ADDR_SERVER, frame, 32);
      break;
    }
  }
}

void forward_server_checks_to_nodes(const uint8_t* frame32) {
  if (!frame32) return;
  if (frame32[0] != proto::CHECK_STATUS_REQ) return;

  // Broadcast: forward to all nodes; each node will reply with STATUS_RESP.
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

  while (radio_try_receive(frame, &pipe)) {
    if (pipe == 1) {
      handle_from_node(frame);
    } else if (pipe == 2) {
      forward_server_checks_to_nodes(frame);
    }
  }

  poll_all_nodes_status();
}

