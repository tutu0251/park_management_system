#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <avr/wdt.h>
#include <string.h>

#include "node_config.h"
#include "node_protocol.h"

namespace {

#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT WDTO_4S
#endif

RF24 radio(node_cfg::RF_CE_PIN, node_cfg::RF_CSN_PIN);

uint8_t g_node_rx_addr[5];
proto::MachineStatus g_machine_status = proto::STATUS_ONLINE;
proto::MachineStatus g_last_pushed_status = proto::STATUS_ONLINE;
uint32_t g_next_status_push_ms = 0;
uint32_t g_transaction_id = 1;
uint8_t g_radio_fail_streak = 0;

void wdt_init() {
  wdt_disable();
  wdt_enable(WDT_TIMEOUT);
}

void pad32(uint8_t* frame, size_t payload_len) {
  if (payload_len >= proto::PAYLOAD_MAX) return;
  memset(frame + payload_len, 0, proto::PAYLOAD_MAX - payload_len);
}

bool radio_begin() {
  if (!radio.begin()) return false;

  radio.setChannel(node_cfg::RF_CHANNEL);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(proto::PAYLOAD_MAX);
  radio.setAutoAck(true);

  node_cfg::node_listen_addr(g_node_rx_addr, node_cfg::kNodeId);

  radio.openReadingPipe(1, g_node_rx_addr);
  radio.openWritingPipe(node_cfg::ADDR_GW_NODE);
  radio.startListening();
  return true;
}

bool send_frame_to_gateway(const uint8_t* payload32) {
  if (!payload32) return false;

  radio.stopListening();
  radio.openWritingPipe(node_cfg::ADDR_GW_NODE);
  const bool ok = radio.write(payload32, proto::PAYLOAD_MAX);
  radio.startListening();

  if (!ok) {
    if (++g_radio_fail_streak >= NODE_RADIO_FAIL_THRESHOLD) {
      g_machine_status = proto::STATUS_ERROR;
    }
    return false;
  }
  g_radio_fail_streak = 0;
  return true;
}

void send_status_to_gateway(const uint8_t* status_payload, size_t len) {
  if (!status_payload || len == 0) return;
  uint8_t frame[proto::PAYLOAD_MAX];
  memset(frame, 0, sizeof(frame));
  const size_t n = len < proto::PAYLOAD_MAX ? len : proto::PAYLOAD_MAX;
  memcpy(frame, status_payload, n);
  send_frame_to_gateway(frame);
}

void on_pay_result(proto::PayResult r, uint32_t credit_cents) {
  Serial.print(F("pay "));
  Serial.print(proto::pay_result_name(r));
  Serial.print(F(" credit_cents="));
  Serial.println(credit_cents);

  if (r == proto::PAY_OK) {
    // Hook: drive reader display / game start with credit_cents
  } else if (r == proto::PAY_FAIL) {
    // Hook: insufficient credit UI
  } else {
    // Hook: error UI / log
  }
}

void handle_check_status() {
  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_response(g_machine_status, buf, sizeof(buf));
  if (n == 0) return;
  Serial.print(F("tx STATUS_RESP "));
  Serial.println(proto::machine_status_name(g_machine_status));
  send_status_to_gateway(buf, n);
}

void process_rx_frame(const uint8_t* frame32) {
  proto::PayResult pr = proto::PAY_ERROR;
  uint32_t credit = 0;
  bool chk = false;

  if (!proto::parse_rx(frame32, proto::PAYLOAD_MAX, &pr, &credit, &chk)) return;

  if (chk) {
    handle_check_status();
    return;
  }

  if (frame32[0] == proto::PAY_RESP) {
    on_pay_result(pr, credit);
  }
}

bool wait_for_pay_response(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    wdt_reset();
    if (!radio.available()) continue;

    uint8_t frame[proto::PAYLOAD_MAX];
    radio.read(frame, sizeof(frame));

    proto::PayResult pr = proto::PAY_ERROR;
    uint32_t credit = 0;
    bool chk = false;
    if (!proto::parse_rx(frame, sizeof(frame), &pr, &credit, &chk)) continue;

    if (chk) {
      handle_check_status();
      continue;
    }

    if (frame[0] == proto::PAY_RESP) {
      on_pay_result(pr, credit);
      return true;
    }
  }
  Serial.println(F("pay timeout"));
  on_pay_result(proto::PAY_ERROR, 0);
  return false;
}

bool send_swipe(const uint8_t* uid, uint8_t uid_len) {
  const uint32_t txn = g_transaction_id++;

  uint8_t frame[proto::PAYLOAD_MAX];
  size_t swipe_len = 0;
  if (!proto::build_swipe(uid, uid_len, node_cfg::kNodeId, node_cfg::kReaderId, txn,
                          node_cfg::kDefaultGameId, frame, sizeof(frame), &swipe_len)) {
    Serial.println(F("build_swipe failed"));
    return false;
  }
  pad32(frame, swipe_len);

  Serial.print(F("tx SWIPE txn="));
  Serial.println(txn);

  if (!send_frame_to_gateway(frame)) {
    Serial.println(F("tx swipe failed"));
    on_pay_result(proto::PAY_ERROR, 0);
    return false;
  }

  return wait_for_pay_response(NODE_PAY_WAIT_MS);
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parse_hex_uid(const char* s, uint8_t* uid_out, uint8_t* uid_len_out) {
  if (!s || !uid_out || !uid_len_out) return false;
  while (*s == ' ' || *s == '\t') ++s;
  const size_t slen = strlen(s);
  if (slen < 2 || (slen % 2) != 0) return false;
  if (slen / 2 > 8) return false;

  for (size_t i = 0; i < slen; i += 2) {
    const int hi = hex_nibble(s[i]);
    const int lo = hex_nibble(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    uid_out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
  }
  *uid_len_out = static_cast<uint8_t>(slen / 2);
  return true;
}

void poll_serial_commands() {
  static char line[48];
  static uint8_t pos = 0;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      line[pos] = '\0';
      pos = 0;
      if (line[0] == '\0') continue;

      if (strncmp(line, "uid ", 4) == 0) {
        uint8_t uid[8];
        uint8_t uid_len = 0;
        if (parse_hex_uid(line + 4, uid, &uid_len)) {
          send_swipe(uid, uid_len);
        } else {
          Serial.println(F("bad uid hex (1-8 bytes, even count)"));
        }
      } else if (strncmp(line, "status ", 7) == 0) {
        const char* arg = line + 7;
        if (strcmp(arg, "online") == 0) {
          g_machine_status = proto::STATUS_ONLINE;
        } else if (strcmp(arg, "maintenance") == 0) {
          g_machine_status = proto::STATUS_MAINTENANCE;
        } else if (strcmp(arg, "offline") == 0) {
          g_machine_status = proto::STATUS_OFFLINE;
        } else if (strcmp(arg, "error") == 0) {
          g_machine_status = proto::STATUS_ERROR;
        } else {
          Serial.println(F("usage: status online|maintenance|offline|error"));
          continue;
        }
        Serial.print(F("machine_status="));
        Serial.println(proto::machine_status_name(g_machine_status));

        uint8_t pbuf[proto::PAYLOAD_MAX];
        const size_t pn = proto::build_status_push(g_machine_status, pbuf, sizeof(pbuf));
        if (pn > 0) {
          send_status_to_gateway(pbuf, pn);
          g_last_pushed_status = g_machine_status;
        }
      } else {
        Serial.println(F("cmds: \"uid <hex>\" | \"status online|...\""));
      }
      continue;
    }
    if (pos < sizeof(line) - 1) line[pos++] = c;
  }
}

void maybe_push_status(uint32_t now_ms) {
  if (static_cast<int32_t>(now_ms - g_next_status_push_ms) < 0) return;
  g_next_status_push_ms = now_ms + NODE_STATUS_PUSH_MS;

  const bool not_online = (g_machine_status != proto::STATUS_ONLINE);
  const bool changed = (g_machine_status != g_last_pushed_status);
  if (!changed && !not_online) return;

  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_push(g_machine_status, buf, sizeof(buf));
  if (n == 0) return;
  Serial.print(F("tx STATUS_PUSH "));
  Serial.println(proto::machine_status_name(g_machine_status));
  send_status_to_gateway(buf, n);
  g_last_pushed_status = g_machine_status;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  wdt_init();

  if (!radio_begin()) {
    Serial.println(F("radio begin failed"));
    g_machine_status = proto::STATUS_ERROR;
  } else {
    Serial.print(F("node_ok id="));
    Serial.print(node_cfg::kNodeId);
    Serial.print(F(" rx_addr=NODE"));
    Serial.write(g_node_rx_addr[4]);
    Serial.println();
  }

  g_next_status_push_ms = millis() + NODE_STATUS_PUSH_MS;
}

void loop() {
  wdt_reset();

  const uint32_t now = millis();

  uint8_t pipe = 0;
  if (radio.available(&pipe)) {
    uint8_t frame[proto::PAYLOAD_MAX];
    radio.read(frame, sizeof(frame));
    process_rx_frame(frame);
  }

  poll_serial_commands();
  maybe_push_status(now);
}
