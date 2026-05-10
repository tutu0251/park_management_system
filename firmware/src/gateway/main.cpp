// =============================================================================
// main.cpp — ATmega8 USB gateway: PC UART ↔ nRF24 ↔ backbone relay
// =============================================================================
//
// BIG-PICTURE LOOP
// ----------------
//   setup():
//       • Bring up UART at GATEWAY_SERIAL_BAUD (must match PC terminal settings).
//       • Arm watchdog — gateway must remain responsive even under RF packet loss.
//       • Seed Arduino pseudo-random generator for TX backoff jitter.
//       • Initialize RF24 pipes + announce `GW<id>_ok` banner on serial.
//
//   loop() (never blocks for long):
//       1) `wdt_reset()` — pet watchdog **before** other work.
//       2) `handle_rf_rx()` — drain entire RF FIFO each iteration (prevents backlog).
//       3) `handle_pc_tx()` — parse at most one PC command if a full line arrived.
//
// SUCCESS / FAILURE SIGNALLING TO THE PC
// --------------------------------------
//   • RF TX success → `ACK,node_id=…` line (scriptable handshake).
//   • RF TX exhaustion → `ERR,…,reason=TX_FAILED` from gateway_serial.cpp.
//
// MULTI-BACKBONE NOTE
// -------------------
// `backbone_tx_addr_for_node` currently ignores `node_id` and always targets the shared
// ADDR_GW_SERVER pipe. If you deploy multiple backbone radios, replace this function with
// a lookup table that maps node_id ranges → distinct five-byte RF addresses.
//
// =============================================================================

#include <Arduino.h>
#include <avr/wdt.h>

#include "gateway_config.h"
#include "gateway_protocol.h"
#include "gateway_radio.h"
#include "gateway_serial.h"

namespace {

// Shared TX/RX scratch — reused for every RF transaction to keep stack tiny on ATmega8.
uint8_t g_frame[gwcfg::kPayloadMax];

// Counts consecutive RF failures; triggers `gw_radio_recover()` after a small threshold.
uint8_t g_tx_fail_streak = 0;

// Address selection hook — central place to grow multi-concentrator routing later.
inline const uint8_t* backbone_tx_addr_for_node(uint16_t /*node_id*/) {
  return gwcfg::ADDR_GW_SERVER;
}

// Minimal positive acknowledgement so hosts know Shockburst + auto-ACK succeeded.
void print_ack_line(const GwPcCommand& cmd) {
  Serial.print(F("ACK,node_id="));
  Serial.print(cmd.node_id);
  Serial.print(F(",reader_id="));
  Serial.print(cmd.reader_id);
  Serial.print(F(",transaction_id="));
  Serial.print(cmd.transaction_id);
  Serial.println();
}

void handle_rf_rx(void) {
  while (gw_radio_receive(g_frame, nullptr)) {
    const uint8_t t = g_frame[0];
    switch (t) {
      case proto::SWIPE_REQ:
        if (proto::swipe_wire_ok(g_frame)) {
          proto::serial_print_rx_swipe(g_frame);
        }
        break;
      case proto::STATUS_PUSH:
      case proto::STATUS_RESP:
        proto::serial_print_rx_status(g_frame, t);
        break;
      default:
        // Ignore unknown opcodes — keeps firmware forward-compatible with extra mesh types.
        break;
    }
  }
}

void handle_pc_tx(void) {
  GwPcCommand cmd{};
  if (!gw_serial_drain_command(&cmd)) return;

  bool ok = false;
  if (cmd.kind == 1) {
    if (!proto::build_pay_frame(&cmd, g_frame)) return;
    ok = gw_radio_send_to_backbone(backbone_tx_addr_for_node(cmd.node_id), g_frame);
  } else if (cmd.kind == 2) {
    if (!proto::build_check_status_frame(&cmd, g_frame)) return;
    ok = gw_radio_send_to_backbone(backbone_tx_addr_for_node(cmd.node_id), g_frame);
  }

  if (ok) {
    g_tx_fail_streak = 0;
    print_ack_line(cmd);
    return;
  }

  if (++g_tx_fail_streak >= 3) {
    gw_radio_recover();
    g_tx_fail_streak = 0;
  }
  gw_serial_print_err_tx_failed(&cmd);
}

}  // namespace

void setup(void) {
  Serial.begin(gwcfg::kSerialBaud);
  gw_serial_begin();

  wdt_enable(GATEWAY_WDTO);

  randomSeed(static_cast<uint32_t>(analogRead(0)) ^ millis());

  if (!gw_radio_setup()) {
    Serial.println(F("radio_begin_failed"));
  }
  gw_serial_print_banner();
}

void loop(void) {
  wdt_reset();

  handle_rf_rx();
  handle_pc_tx();
}
