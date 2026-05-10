// =============================================================================
// gateway_radio.cpp — nRF24L01+ transport layer for the USB gateway MCU
// =============================================================================
//
// ROLE IN THE PARK NETWORK
// ------------------------
// The gateway is the **only** firmware image that speaks UART to the billing PC while
// participating on the same RF channel / address plan as backbone relays. Reader nodes
// never talk to the PC directly — they stop at the backbone hop.
//
// ADDRESSING / PIPES (must stay aligned with backbone_radio.cpp)
// -----------------------------------------------------------------
// LISTEN (RX):
//   Pipe 1 subscribed to ADDR_SERVER — backbone concentrates uplink traffic here.
//
// WRITE (TX):
//   Default pipe opened to ADDR_GW_SERVER — backbone listens on **its** RX pipe #2 for
//   PC-originated commands relayed toward readers.
//
// RF24 API GOTCHA — setRetries(delay, count)
// ------------------------------------------
// Nordic documentation names can confuse newcomers: RF24 expects `(retransmitDelay,
// retransmitCount)` **in that order**. Swapping them silently changes RF reliability.
//
// RETRY STRATEGY
// --------------
// Layer 1: RF24 auto-ACK + hardware retries from `setRetries`.
// Layer 2: `gw_radio_send_to_backbone` loops `kAppTxRetries` times with random backoff
//           milliseconds between attempts to reduce synchronized collisions with peers.
//
// RECOVERY PATH
// -------------
// After repeated failures main.cpp calls `gw_radio_recover()` — power-cycle RF front-end,
// reapply tuning registers, reopen pipes. Keeps gateway alive without full MCU reset.
//
// =============================================================================

#include "gateway_radio.h"

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <string.h>

#include "gateway_config.h"

namespace {

RF24 radio(gwcfg::kRfCePin, gwcfg::kRfCsnPin);

// Shared RF tuning applied both at boot and after recover().
bool apply_common_rf_settings(void) {
  if (!radio.begin()) return false;
  radio.setChannel(gwcfg::kRfChannel);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_1MBPS);
  radio.setRetries(gwcfg::kRfRetryDelay, gwcfg::kRfRetryArc);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(gwcfg::kPayloadMax);
  radio.setAutoAck(true);
  return true;
}

}  // namespace

bool gw_radio_setup(void) {
  SPI.begin();
  if (!apply_common_rf_settings()) return false;

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

    radio.flush_tx();
  }
  return false;
}

void gw_radio_recover(void) {
  radio.powerDown();
  delay(2);
  radio.powerUp();
  delay(2);
  (void)apply_common_rf_settings();
  radio.openReadingPipe(1, gwcfg::ADDR_SERVER);
  radio.openWritingPipe(gwcfg::ADDR_GW_SERVER);
  radio.startListening();
}
