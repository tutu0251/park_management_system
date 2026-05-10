// -----------------------------------------------------------------------------
// gateway_radio.cpp — hardware SPI RF24, minimal SRAM, blocking write w/ retry
// -----------------------------------------------------------------------------
#include "gateway_radio.h"

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <string.h>

#include "gateway_config.h"

namespace {

RF24 radio(gwcfg::kRfCePin, gwcfg::kRfCsnPin);

bool apply_common_rf_settings() {
  if (!radio.begin()) return false;
  radio.setChannel(gwcfg::kRfChannel);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(gwcfg::kRfRetryArc, gwcfg::kRfRetryDelay);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(gwcfg::kPayloadMax);
  radio.setAutoAck(true);
  return true;
}

}  // namespace

bool gw_radio_setup() {
  SPI.begin();
  if (!apply_common_rf_settings()) return false;

  // Pipe 1: uplink frames from backbone toward the PC path (ADDR_SERVER).
  radio.openReadingPipe(1, gwcfg::ADDR_SERVER);
  radio.openWritingPipe(gwcfg::ADDR_GW_SERVER);
  radio.startListening();
  return true;
}

bool gw_radio_receive(uint8_t* out32, uint8_t* pipe_out) {
  if (!out32) return false;
  uint8_t pipe = 0;
  if (!radio.available(&pipe)) return false;
  radio.read(out32, gwcfg::kPayloadMax);
  if (pipe_out) *pipe_out = pipe;
  return true;
}

bool gw_radio_send_to_backbone(const uint8_t dest_addr[5], const uint8_t frame32[32]) {
  if (!dest_addr || !frame32) return false;

  for (uint8_t attempt = 0; attempt < gwcfg::kAppTxRetries; ++attempt) {
    if (attempt > 0) {
      const uint8_t ms = 1 + static_cast<uint8_t>(random() % gwcfg::kTxBackoffMsMax);
      delay(ms);
    }

    radio.stopListening();
    radio.openWritingPipe(dest_addr);
    const bool ok = radio.write(frame32, gwcfg::kPayloadMax);
    radio.startListening();
    if (ok) return true;

    // Flush failed TX state before the next attempt.
    radio.flush_tx();
  }
  return false;
}

void gw_radio_recover() {
  radio.powerDown();
  delay(2);
  radio.powerUp();
  delay(2);
  (void)apply_common_rf_settings();
  radio.openReadingPipe(1, gwcfg::ADDR_SERVER);
  radio.openWritingPipe(gwcfg::ADDR_GW_SERVER);
  radio.startListening();
}
