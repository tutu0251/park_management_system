// =============================================================================
// main.cpp — ATmega8 USB-serial gateway between PC and park backbone radios
// =============================================================================
// Flow:
//  1) setup(): UART, watchdog, RF24 listening on ADDR_SERVER (backbone uplink).
//  2) loop(): feed WDT, drain nRF24 RX -> print compact "RX,..." CSV lines.
//  3) loop(): drain UART lines beginning with "TX," -> build 32-byte wire frame
//     -> transmit toward ADDR_GW_SERVER (backbone pipe 2) with backoff retries.
// On-air layout matches backbone_firmware/node_firmware node_protocol.h.
// =============================================================================

#include <Arduino.h>
#include <avr/wdt.h>
#include "gateway_config.h"
#include "gateway_protocol.h"
#include "gateway_radio.h"
#include "gateway_serial.h"

namespace {

uint8_t g_frame[gwcfg::kPayloadMax];
uint8_t g_tx_fail_streak = 0;

inline const uint8_t* backbone_tx_addr_for_node(uint16_t /*node_id*/) {
  // Single-backbone default: all PC commands target the shared downlink address.
  // Extend with a PROGMEM routing table here if multiple backbone radios exist.
  return gwcfg::ADDR_GW_SERVER;
}

void print_ack_line(const GwPcCommand& cmd) {
  Serial.print(F("ACK,node_id="));
  Serial.print(cmd.node_id);
  Serial.print(F(",reader_id="));
  Serial.print(cmd.reader_id);
  Serial.print(F(",transaction_id="));
  Serial.print(cmd.transaction_id);
  Serial.println();
}

void handle_rf_rx() {
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
        break;
    }
  }
}

void handle_pc_tx() {
  GwPcCommand cmd{};
  if (!gw_serial_drain_command(&cmd)) return;

  bool ok = false;
  if (cmd.kind == 1) {
    const uint32_t credit = cmd.has_credit ? cmd.credit_remain : 0UL;
    if (!proto::build_pay_frame(cmd.pay_result, credit, g_frame)) return;
    ok = gw_radio_send_to_backbone(backbone_tx_addr_for_node(cmd.node_id), g_frame);
  } else if (cmd.kind == 2) {
    if (!proto::build_check_status_frame(g_frame)) return;
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

void setup() {
  Serial.begin(gwcfg::kSerialBaud);
  gw_serial_begin();

  wdt_enable(GATEWAY_WDTO);

  randomSeed(static_cast<uint32_t>(analogRead(0)) ^ millis());

  if (!gw_radio_setup()) {
    Serial.println(F("radio_begin_failed"));
  }
  gw_serial_print_banner();
}

void loop() {
  wdt_reset();

  handle_rf_rx();
  handle_pc_tx();
}
