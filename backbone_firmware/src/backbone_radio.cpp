// -----------------------------------------------------------------------------
// backbone_radio.cpp — hardware-SPI nRF24 driver, fixed 32-byte frames
// -----------------------------------------------------------------------------
//
// PIPE TOPOLOGY (must agree with node_radio.cpp + gateway_radio.cpp)
// ------------------------------------------------------------------
//   RX pipe 1  →  bbcfg::ADDR_GW_NODE     ("GWAY1") — frames from reader nodes
//   RX pipe 2  →  bbcfg::ADDR_GW_SERVER   ("GWSV1") — frames from the gateway
//   TX (default after begin) → bbcfg::ADDR_SERVER  ("SERV1") — toward gateway
//
// WHY TWO RX PIPES
// ----------------
// Disambiguates uplink vs downlink in software (pipe index returned by
// RF24::available()) without walking the payload byte. Cheap and reliable.
//
// SEND PROCEDURE
// --------------
// 1) stopListening()   — RF24 requires this before write().
// 2) openWritingPipe(addr) — switch destination per call (gateway or reader).
// 3) write(frame, 32)  — full payload width every time.
// 4) startListening()  — restore RX so we don't miss the next downlink.
// On failure: optionally flush_tx and retry with random backoff (≤ kAppTxRetries).
//
// COLLISION MITIGATION
// --------------------
// Hardware auto-ACK + retries handle ShockBurst-layer contention. The app
// retry loop adds millisecond-granularity jitter so multiple readers TXing
// simultaneously do not synchronize their backoffs and live-lock the channel.
//
// -----------------------------------------------------------------------------

#include "backbone_radio.h"

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <string.h>

#include "backbone_config.h"

namespace {

RF24 g_radio(bbcfg::kRfCePin, bbcfg::kRfCsnPin);

// Writes the entire on-air configuration; called from begin() and recover().
// Returns false only if the SPI/nRF24 chip identity check inside begin() fails.
bool apply_common_rf_settings() {
  if (!g_radio.begin()) return false;

  g_radio.setChannel(bbcfg::kRfChannel);
  g_radio.setPALevel(RF24_PA_HIGH);
  g_radio.setDataRate(RF24_1MBPS);
  g_radio.setRetries(bbcfg::kRfRetryDelay, bbcfg::kRfRetryArc);
  g_radio.setCRCLength(RF24_CRC_16);
  g_radio.setPayloadSize(bbcfg::kPayloadMax);
  g_radio.setAutoAck(true);
  return true;
}

// Tiny LFSR-style mix — not cryptographic; only spreads retry jitter.
// Avoids pulling in stdlib's random() machinery.
uint16_t prng16() {
  static uint16_t s = 0xACE1u;
  uint16_t x = s;
  x ^= static_cast<uint16_t>(x << 7);
  x ^= static_cast<uint16_t>(x >> 9);
  x ^= static_cast<uint16_t>(x << 8);
  s = x;
  return x;
}

}  // namespace

bool backbone_radio_begin() {
  SPI.begin();
  if (!apply_common_rf_settings()) return false;

  // Reader-side uplink frames land on pipe 1; gateway downlink on pipe 2.
  g_radio.openReadingPipe(1, bbcfg::ADDR_GW_NODE);
  g_radio.openReadingPipe(2, bbcfg::ADDR_GW_SERVER);

  // Default TX target: gateway. Per-call openWritingPipe() switches destination.
  g_radio.openWritingPipe(bbcfg::ADDR_SERVER);
  g_radio.startListening();
  return true;
}

bool backbone_radio_try_recv(uint8_t out32[32], uint8_t* pipe_out) {
  if (!out32) return false;

  uint8_t pipe = 0;
  if (!g_radio.available(&pipe)) return false;

  g_radio.read(out32, bbcfg::kPayloadMax);
  if (pipe_out) *pipe_out = pipe;
  return true;
}

bool backbone_radio_send_to(const uint8_t addr[5], const uint8_t frame32[32]) {
  if (!addr || !frame32) return false;

  for (uint8_t attempt = 0; attempt < bbcfg::kAppTxRetries; ++attempt) {
    if (attempt > 0) {
      // 1 .. kTxBackoffMsMax milliseconds of pseudo-random jitter.
      const uint16_t r = prng16();
      const uint8_t ms = static_cast<uint8_t>(1U + (r % bbcfg::kTxBackoffMsMax));
      delay(ms);
    }

    g_radio.stopListening();
    g_radio.openWritingPipe(addr);
    const bool ok = g_radio.write(frame32, bbcfg::kPayloadMax);
    g_radio.startListening();

    if (ok) return true;
    g_radio.flush_tx();   // Drop the failed FIFO entry before the next try.
  }
  return false;
}

void backbone_radio_recover() {
  // Power cycle the chip and rebuild the configuration. Cheap insurance after
  // long bursts of TX failure (hot SPI, brownouts, antenna detune events).
  g_radio.powerDown();
  delay(2);
  g_radio.powerUp();
  delay(2);

  (void)apply_common_rf_settings();
  g_radio.openReadingPipe(1, bbcfg::ADDR_GW_NODE);
  g_radio.openReadingPipe(2, bbcfg::ADDR_GW_SERVER);
  g_radio.openWritingPipe(bbcfg::ADDR_SERVER);
  g_radio.startListening();
}
