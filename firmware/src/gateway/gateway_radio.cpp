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

// Public API declarations for RF setup, RX, TX, and recovery.
#include "gateway_radio.h"

// `Serial`, `delay`, `random` for backoff — Arduino runtime on AVR.
#include <Arduino.h>
// Nordic nRF24L01+ driver instance type used below.
#include <RF24.h>
// Shared SPI bus used by RF24 on ATmega8 shield wiring.
#include <SPI.h>
// Present here for consistency with other gateway sources that mix RF + C helpers.
#include <string.h>

// CE/CSN pins, channel, payload size, retry counts, and ADDR_* arrays.
#include "gateway_config.h"

namespace {

// Global RF24 driver bound to board pins from `gwcfg` — one radio object for whole gateway firmware.
RF24 radio(gwcfg::kRfCePin, gwcfg::kRfCsnPin);

// Shared RF tuning applied both at boot and after recover().
bool apply_common_rf_settings(void) {
  // SPI and IRQ wiring must be valid; false means hardware fault or bad pins.
  if (!radio.begin()) return false;
  // Must match backbone + nodes so packets are not received on wrong channel.
  radio.setChannel(gwcfg::kRfChannel);
  // Gateway is mains-powered — maximize link budget toward backbone.
  radio.setPALevel(RF24_PA_MAX);
  // 1 Mbps is the park network default; keep identical across images.
  radio.setDataRate(RF24_1MBPS);
  // Hardware auto-retry schedule: delay slots × ARC count (order is RF24-specific).
  radio.setRetries(gwcfg::kRfRetryDelay, gwcfg::kRfRetryArc);
  // Stronger CRC catches more corruption before software parses garbage opcodes.
  radio.setCRCLength(RF24_CRC_16);
  // Fixed 32-byte Shockburst width — must match `kPayloadMax` everywhere.
  radio.setPayloadSize(gwcfg::kPayloadMax);
  // Enables Shockburst ACK payloads path and PRX/PTX handshake expectations.
  radio.setAutoAck(true);
  return true;
}

}  // namespace

bool gw_radio_setup(void) {
  // Initializes SCK/MOSI/MISO and sets SPI mode expected by RF24 library.
  SPI.begin();
  // Abort early if the transceiver never responds on SPI.
  if (!apply_common_rf_settings()) return false;

  // Backbone uplink concentrated to ADDR_SERVER — gateway listens on logical pipe 1.
  radio.openReadingPipe(1, gwcfg::ADDR_SERVER);
  // Default TX pipe toward backbone’s GW-server listener.
  radio.openWritingPipe(gwcfg::ADDR_GW_SERVER);
  // Gateway spends most time receiving backbone → PC telemetry.
  radio.startListening();
  return true;
}

bool gw_radio_receive(uint8_t* out32, uint8_t* pipe_out) {
  // Caller must supply buffer sized for fixed 32-byte payloads.
  if (!out32) return false;
  uint8_t pipe = 0;
  // RF24 reports which reading pipe matched — optional for gateway logging.
  if (!radio.available(&pipe)) return false;
  // Blocking read of exactly `kPayloadMax` bytes — payload size was configured in setup.
  radio.read(out32, gwcfg::kPayloadMax);
  // Caller may pass nullptr if pipe index is irrelevant.
  if (pipe_out) *pipe_out = pipe;
  return true;
}

bool gw_radio_send_to_backbone(const uint8_t dest_addr[5], const uint8_t frame32[32]) {
  // Guard against host bugs passing null pointers into C API.
  if (!dest_addr || !frame32) return false;

  // Application-level retry loop on top of Nordic auto-retry — survives burst collisions.
  for (uint8_t attempt = 0; attempt < gwcfg::kAppTxRetries; ++attempt) {
    if (attempt > 0) {
      // Random jitter spreads retries when multiple gateways TX near-simultaneously.
      const uint8_t ms = 1 + static_cast<uint8_t>(random() % gwcfg::kTxBackoffMsMax);
      delay(ms);
    }

    // TX requires PRX mode off — briefly deaf to backbone during write.
    radio.stopListening();
    // Dynamic pipe target allows future per-node routing tables without re-plumbing API shape.
    radio.openWritingPipe(dest_addr);
    // `write` returns true when ACK was received for this 32-byte packet.
    const bool ok = radio.write(frame32, gwcfg::kPayloadMax);
    // Return to RX ASAP so uplink telemetry is not dropped longer than necessary.
    radio.startListening();
    if (ok) return true;

    // Clear stuck FIFO state before the next attempt.
    radio.flush_tx();
  }
  return false;
}

void gw_radio_recover(void) {
  // Hard reset of front-end regulator/PLL path per Nordic app notes.
  radio.powerDown();
  delay(2);
  radio.powerUp();
  delay(2);
  // Reapply channel, rate, CRC, retries — mirrors successful setup path.
  (void)apply_common_rf_settings();
  // Recreate pipe bindings in case library lost shadow registers.
  radio.openReadingPipe(1, gwcfg::ADDR_SERVER);
  radio.openWritingPipe(gwcfg::ADDR_GW_SERVER);
  radio.startListening();
}
