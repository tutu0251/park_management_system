// =============================================================================
// main.cpp — park payment NODE: RFID reader radio + Serial test harness (AVR)
// =============================================================================
// Responsibilities:
// - Listen on per-node nRF24 address for PAY_RESP and CHECK_STATUS_REQ.
// - Transmit SWIPE_REQ and status frames to gateway address.
// - Watchdog reset in long waits; optional periodic STATUS_PUSH when not healthy.
// =============================================================================

#include <Arduino.h>    // setup(), loop(), millis(), Serial, delay
#include <RF24.h>       // nRF24L01 driver
#include <SPI.h>        // SPI bus used by RF24
#include <avr/wdt.h>    // Watchdog enable/disable/reset for lockup recovery
#include <string.h>     // memset, memcpy, strlen, strncmp, strcmp

#include "node_config.h"    // Pins, NODE_ID, ADDR_GW_NODE, node_listen_addr
#include "node_protocol.h"  // Frame builders/parsers and enums

namespace {

// Anonymous namespace: internal linkage for helpers/globals below TU scope.

#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT WDTO_4S   // Default AVR WDT period if not overridden by build.
#endif

// Global RF24 instance: CE/CSN pins from node_cfg (see node_config.h).
RF24 radio(node_cfg::RF_CE_PIN, node_cfg::RF_CSN_PIN);

// Five-byte RX address for this node; filled in radio_begin() from NODE_ID.
uint8_t g_node_rx_addr[5];

// Current machine status (operator / fault state) used in status messages.
proto::MachineStatus g_machine_status = proto::STATUS_ONLINE;

// Last status successfully pushed; used to avoid spamming "online" pushes.
proto::MachineStatus g_last_pushed_status = proto::STATUS_ONLINE;

// millis() deadline for next maybe_push_status() attempt.
uint32_t g_next_status_push_ms = 0;

// Increments each swipe for correlation in logs / server idempotency hooks.
uint32_t g_transaction_id = 1;

// Count of consecutive radio TX failures; cleared on success; drives STATUS_ERROR.
uint8_t g_radio_fail_streak = 0;

// Configure and enable hardware watchdog so runaway loops reset the MCU.
void wdt_init() {
  wdt_disable();              // Must disable before changing timeout selection.
  wdt_enable(WDT_TIMEOUT);    // Reset chip if wdt_reset() not called in time.
}

// Zero-fill remainder of a 32-byte frame so on-air CRC covers explicit padding.
void pad32(uint8_t* frame, size_t payload_len) {
  if (payload_len >= proto::PAYLOAD_MAX) return;  // Already full or overfull.
  memset(frame + payload_len, 0, proto::PAYLOAD_MAX - payload_len);
}

// One-time RF24 configuration: channel, CRC, pipes, fixed payload size.
bool radio_begin() {
  if (!radio.begin()) return false;   // SPI / wiring / power problem.

  radio.setChannel(node_cfg::RF_CHANNEL);   // Must match gateway and peers.
  radio.setPALevel(RF24_PA_HIGH);           // Range vs power tradeoff.
  radio.setDataRate(RF24_1MBPS);            // 1 Mbps is robust default for nRF24.
  radio.setRetries(5, 15);                // Auto-retry before reporting TX fail.
  radio.setCRCLength(RF24_CRC_16);          // 16-bit CRC on hardware packet.
  radio.setPayloadSize(proto::PAYLOAD_MAX); // Fixed 32-byte dynamic payloads.
  radio.setAutoAck(true);                   // ShockBurst ACK on pipe traffic.

  // Compute NODE{n} listen address into g_node_rx_addr.
  node_cfg::node_listen_addr(g_node_rx_addr, node_cfg::kNodeId);

  radio.openReadingPipe(1, g_node_rx_addr);       // Pipe 1: inbound from gateway.
  radio.openWritingPipe(node_cfg::ADDR_GW_NODE); // Default TX: toward gateway.
  radio.startListening();                       // RX mode until we explicitly TX.
  return true;
}

// Stop listening, TX one full 32-byte frame to gateway, resume listening.
bool send_frame_to_gateway(const uint8_t* payload32) {
  if (!payload32) return false;   // Null guard.

  radio.stopListening();                          // Chip must be in TX mode.
  radio.openWritingPipe(node_cfg::ADDR_GW_NODE); // Reinforce dest address.
  const bool ok = radio.write(payload32, proto::PAYLOAD_MAX); // Blocking send.
  radio.startListening();                         // Return to RX for downlink.

  if (!ok) {
    if (++g_radio_fail_streak >= NODE_RADIO_FAIL_THRESHOLD) {
      g_machine_status = proto::STATUS_ERROR;   // Surface RF problems upstream.
    }
    return false;
  }
  g_radio_fail_streak = 0;        // Successful ACK path resets failure counter.
  return true;
}

// Build a zero-padded 32-byte buffer from a shorter status payload and send it.
void send_status_to_gateway(const uint8_t* status_payload, size_t len) {
  if (!status_payload || len == 0) return;

  uint8_t frame[proto::PAYLOAD_MAX];
  memset(frame, 0, sizeof(frame));   // Ensure tail is explicit zeros.
  const size_t n = len < proto::PAYLOAD_MAX ? len : proto::PAYLOAD_MAX;
  memcpy(frame, status_payload, n);  // Copy actual STATUS_* bytes at front.
  send_frame_to_gateway(frame);
}

// Application hook: map pay result to UI / game start (placeholder comments).
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

// Respond to gateway poll: send STATUS_RESP with current g_machine_status.
void handle_check_status() {
  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_response(g_machine_status, buf, sizeof(buf));
  if (n == 0) return;   // Buffer too small (should not happen with PAYLOAD_MAX).

  Serial.print(F("tx STATUS_RESP "));
  Serial.println(proto::machine_status_name(g_machine_status));
  send_status_to_gateway(buf, n);
}

// Dispatch a single 32-byte RX frame from gateway (PAY_RESP or status poll).
void process_rx_frame(const uint8_t* frame32) {
  proto::PayResult pr = proto::PAY_ERROR;
  uint32_t credit = 0;
  bool chk = false;

  if (!proto::parse_rx(frame32, proto::PAYLOAD_MAX, &pr, &credit, &chk)) return;

  if (chk) {
    handle_check_status();   // CHECK_STATUS_REQ path — no PAY_RESP body.
    return;
  }

  if (frame32[0] == proto::PAY_RESP) {  // Redundant with parse_rx but explicit.
    on_pay_result(pr, credit);
  }
}

// Blocking wait for PAY_RESP after a swipe TX; feeds WDT while polling radio.
bool wait_for_pay_response(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;   // Unsigned wraparound-safe compare below.

  while (static_cast<int32_t>(millis() - deadline) < 0) {  // now < deadline
    wdt_reset();                   // Stay alive while potentially blocking long.
    if (!radio.available()) continue;

    uint8_t frame[proto::PAYLOAD_MAX];
    radio.read(frame, sizeof(frame));   // Drain one fixed-size packet.

    proto::PayResult pr = proto::PAY_ERROR;
    uint32_t credit = 0;
    bool chk = false;
    if (!proto::parse_rx(frame, sizeof(frame), &pr, &credit, &chk)) continue;

    if (chk) {
      handle_check_status();   // Interleaved status poll during pay wait.
      continue;
    }

    if (frame[0] == proto::PAY_RESP) {
      on_pay_result(pr, credit);
      return true;             // Normal completion.
    }
  }
  Serial.println(F("pay timeout"));
  on_pay_result(proto::PAY_ERROR, 0);   // Map timeout to error result for UI.
  return false;
}

// Build SWIPE_REQ, pad to 32 bytes, TX, then block for PAY_RESP.
bool send_swipe(const uint8_t* uid, uint8_t uid_len) {
  const uint32_t txn = g_transaction_id++;   // Post-increment: use then advance.

  uint8_t frame[proto::PAYLOAD_MAX];
  size_t swipe_len = 0;
  if (!proto::build_swipe(uid, uid_len, node_cfg::kNodeId, node_cfg::kReaderId, txn,
                          node_cfg::kDefaultGameId, frame, sizeof(frame), &swipe_len)) {
    Serial.println(F("build_swipe failed"));
    return false;
  }
  pad32(frame, swipe_len);   // RF layer always sends full width.

  Serial.print(F("tx SWIPE txn="));
  Serial.println(txn);

  if (!send_frame_to_gateway(frame)) {
    Serial.println(F("tx swipe failed"));
    on_pay_result(proto::PAY_ERROR, 0);
    return false;
  }

  return wait_for_pay_response(NODE_PAY_WAIT_MS);
}

// Parse one hex digit; returns -1 if not a valid hex character.
static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parse even-length hex string into up to 8 UID bytes for Serial test "uid ...".
bool parse_hex_uid(const char* s, uint8_t* uid_out, uint8_t* uid_len_out) {
  if (!s || !uid_out || !uid_len_out) return false;
  while (*s == ' ' || *s == '\t') ++s;   // Skip leading whitespace.

  const size_t slen = strlen(s);
  if (slen < 2 || (slen % 2) != 0) return false;   // Need full bytes (pairs).
  if (slen / 2 > 8) return false;                  // Protocol max UID storage.

  for (size_t i = 0; i < slen; i += 2) {
    const int hi = hex_nibble(s[i]);
    const int lo = hex_nibble(s[i + 1]);
    if (hi < 0 || lo < 0) return false;            // Illegal character in hex.
    uid_out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
  }
  *uid_len_out = static_cast<uint8_t>(slen / 2);
  return true;
}

// Non-blocking Serial line reader: commands "uid <hex>" and "status <word>".
void poll_serial_commands() {
  static char line[48];     // Line buffer reused across calls.
  static uint8_t pos = 0;   // Current write index into line.

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r') continue;   // Ignore CR; treat LF as line end only.

    if (c == '\n') {
      line[pos] = '\0';   // Terminate C string.
      pos = 0;            // Reset for next line.

      if (line[0] == '\0') continue;   // Ignore empty lines.

      if (strncmp(line, "uid ", 4) == 0) {
        uint8_t uid[8];
        uint8_t uid_len = 0;
        if (parse_hex_uid(line + 4, uid, &uid_len)) {
          send_swipe(uid, uid_len);    // Synthetic card tap.
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
          continue;   // Do not push unknown keyword as status.
        }
        Serial.print(F("machine_status="));
        Serial.println(proto::machine_status_name(g_machine_status));

        uint8_t pbuf[proto::PAYLOAD_MAX];
        const size_t pn = proto::build_status_push(g_machine_status, pbuf, sizeof(pbuf));
        if (pn > 0) {
          send_status_to_gateway(pbuf, pn);   // Immediate notify on manual change.
          g_last_pushed_status = g_machine_status;
        }
      } else {
        Serial.println(F("cmds: \"uid <hex>\" | \"status online|...\""));
      }
      continue;
    }

    if (pos < sizeof(line) - 1) line[pos++] = c;   // Append char if room (reserve NUL).
  }
}

// Time-based STATUS_PUSH for degraded states or after status transitions.
void maybe_push_status(uint32_t now_ms) {
  if (static_cast<int32_t>(now_ms - g_next_status_push_ms) < 0) return;  // Not yet due.
  g_next_status_push_ms = now_ms + NODE_STATUS_PUSH_MS;   // Schedule next window.

  const bool not_online = (g_machine_status != proto::STATUS_ONLINE);
  const bool changed = (g_machine_status != g_last_pushed_status);
  if (!changed && !not_online) return;   // Quiet path: healthy and stable.

  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_push(g_machine_status, buf, sizeof(buf));
  if (n == 0) return;

  Serial.print(F("tx STATUS_PUSH "));
  Serial.println(proto::machine_status_name(g_machine_status));
  send_status_to_gateway(buf, n);
  g_last_pushed_status = g_machine_status;
}

}  // namespace

// Arduino entry: hardware + radio init and first status push schedule.
void setup() {
  Serial.begin(115200);   // Match monitor_speed in platformio.ini.
  delay(50);              // Brief USB-Serial settle on some Nano clones.

  wdt_init();

  if (!radio_begin()) {
    Serial.println(F("radio begin failed"));
    g_machine_status = proto::STATUS_ERROR;   // Report inability to join RF net.
  } else {
    Serial.print(F("node_ok id="));
    Serial.print(node_cfg::kNodeId);
    Serial.print(F(" rx_addr=NODE"));
    Serial.write(g_node_rx_addr[4]);   // Print ASCII digit of node id from address.
    Serial.println();
  }

  g_next_status_push_ms = millis() + NODE_STATUS_PUSH_MS;   // Stagger first push.
}

// Arduino main loop: RX radio, Serial commands, periodic status uplink.
void loop() {
  wdt_reset();   // Pet watchdog every iteration.

  const uint32_t now = millis();

  uint8_t pipe = 0;   // Which reading pipe produced data (informational here).
  if (radio.available(&pipe)) {
    uint8_t frame[proto::PAYLOAD_MAX];
    radio.read(frame, sizeof(frame));
    process_rx_frame(frame);
  }

  poll_serial_commands();
  maybe_push_status(now);
}
