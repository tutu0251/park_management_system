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

// Core Arduino APIs — Serial, millis, analogRead, randomSeed.
#include <Arduino.h>
// Hardware watchdog — resets MCU if loop stalls (RF hang, blocked UART, etc.).
#include <avr/wdt.h>

// Baud, payload size, ADDR_* arrays, watchdog timeout macro.
#include "gateway_config.h"
// RF frame printers + builders for PAY_RESP / CHECK_STATUS_REQ.
#include "gateway_protocol.h"
// nRF24 setup, RX drain, TX with retries, recover().
#include "gateway_radio.h"
// Non-blocking PC line parser producing GwPcCommand.
#include "gateway_serial.h"

namespace {

// Shared TX/RX scratch — reused for every RF transaction to keep stack tiny on ATmega8.
uint8_t g_frame[gwcfg::kPayloadMax];

// Counts consecutive RF failures; triggers `gw_radio_recover()` after a small threshold.
uint8_t g_tx_fail_streak = 0;

// Address selection hook — central place to grow multi-concentrator routing later.
inline const uint8_t* backbone_tx_addr_for_node(uint16_t /*node_id*/) {
  // Today every node routes via the same backbone pipe — replace with lookup when scaled out.
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
  // Drain entire RX FIFO each visit so bursts do not backlog indefinitely.
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
  // Returns false when no full line or parse failure — cheap hot path.
  if (!gw_serial_drain_command(&cmd)) return;

  bool ok = false;
  if (cmd.kind == 1) {
    // Payment response toward reader path — requires card + txn correlation fields.
    if (!proto::build_pay_frame(&cmd, g_frame)) return;
    ok = gw_radio_send_to_backbone(backbone_tx_addr_for_node(cmd.node_id), g_frame);
  } else if (cmd.kind == 2) {
    // Machine status poll forwarded by backbone — node_id must be non-zero from parser.
    if (!proto::build_check_status_frame(&cmd, g_frame)) return;
    ok = gw_radio_send_to_backbone(backbone_tx_addr_for_node(cmd.node_id), g_frame);
  }

  if (ok) {
    g_tx_fail_streak = 0;
    print_ack_line(cmd);
    return;
  }

  // Three strikes triggers RF front-end power cycle without rebooting whole sketch.
  if (++g_tx_fail_streak >= 3) {
    gw_radio_recover();
    g_tx_fail_streak = 0;
  }
  gw_serial_print_err_tx_failed(&cmd);
}

}  // namespace

void setup(void) {
  // Must match host terminal / billing script serial settings.
  Serial.begin(gwcfg::kSerialBaud);
  // Clears partial-line reassembly state before first loop iteration.
  gw_serial_begin();

  // If loop ever wedges, MCU resets within GATEWAY_WDTO (see gateway_config.h).
  wdt_enable(GATEWAY_WDTO);

  // Entropy for RF backoff — floating pin noise XOR uptime milliseconds.
  randomSeed(static_cast<uint32_t>(analogRead(0)) ^ millis());

  if (!gw_radio_setup()) {
    Serial.println(F("radio_begin_failed"));
  }
  // Always announce UART readiness — debugging cue even when RF failed above.
  gw_serial_print_banner();
}

void loop(void) {
  // First instruction each tick proves firmware is alive to watchdog hardware.
  wdt_reset();

  handle_rf_rx();
  handle_pc_tx();
}
