// =============================================================================
// node_radio.cpp — RF24 bring-up, Shockburst policy, retry + jitter helpers
// =============================================================================
//
// AUTO ACK AND Shockburst RETRIES (WHY BOTH LAYERS EXIST)
// --------------------------------------------------------
// Layer A — RF24 hardware auto-ACK:
//   Many radios share 2.4 GHz. Short ACK frames confirm that THIS addressed packet
//   survived the immediate collision window and entered the peer’s FIFO. Without auto
//   ACK, application firmware would need its own stop-and-wait ACK packets, costing
//   airtime and flash complexity.
//
// Layer B — RF24 setRetries(delay, count):
//   Transient bursts (wifi/microwave/multipath) lose packets. A bounded automatic
//   retransmit schedule recovers without involving billing logic — still cheap on-air
//   because payloads are only 32 bytes (see PAYLOAD_MAX rationale).
//
// Layer C — NODE_RADIO_APP_TX_RETRIES in node_radio_send_frame():
//   If the hardware retry budget is exhausted, we STILL may want another whole WRITE
//   attempt after a pseudo-random pause so colliding neighbor nodes do not lock-step
//   into perpetual collisions (“retry storm synchronization”).
//
// WHY RANDOM BACKOFF (prng16)
// ---------------------------
// Fixed delays cause synchronized retransmissions when many devices fail together.
// Jitter separates transmit edges in time, reducing repeated collisions.
//
// WHY LIGHWEIGHT PRNG (NOT rand() / crypto RNG)
// ---------------------------------------------
// AVR libc PRNG pulls in globals and codeSize we do not need. We only require
// uncorrelated-enough spacing for milliseconds-scale backoff — not cryptographic keys.
//
// SPI BUS SHARING
// ---------------
// RF24 holds CSN low only during byte transfers; ensure no other SPI slave shares the
// bus without coordinated chip-select discipline.
//
// =============================================================================

#include "node_radio.h"

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#include <string.h>

#include "node_config.h"

static RF24 g_radio(node_cfg::RF_CE_PIN, node_cfg::RF_CSN_PIN);

/// PRNG state lives in static SRAM (not stack) to keep call frames tiny on ATmega8.
static uint16_t g_prng = 0xBEEF;

uint16_t prng16(void) {
  // Classic xorshift16 — a few XOR/shift ops: fast, fixed time, no division.
  g_prng ^= static_cast<uint16_t>(g_prng << 7);
  g_prng ^= static_cast<uint16_t>(g_prng >> 9);
  g_prng ^= static_cast<uint16_t>(g_prng << 8);
  return g_prng;
}

void prng_seed(uint16_t seed) {
  if (seed) g_prng ^= seed;
}

static void pad_payload(uint8_t* dst, uint8_t logical_len) {
  if (logical_len >= proto::PAYLOAD_MAX) return;
  memset(dst + logical_len, 0, proto::PAYLOAD_MAX - logical_len);
}

bool node_radio_begin(void) {
  if (!g_radio.begin()) return false;

  // Channel index must match backbone deployment (see NODE_RF_CHANNEL build flag).
  g_radio.setChannel(node_cfg::RF_CHANNEL);

  // PA level conservative by default — upgrade after benching supply and antenna return loss.
  g_radio.setPALevel(RF24_PA_LOW);

  // 1 Mbps is a good balance of range vs packet duration (shorter packets => fewer collisions).
  g_radio.setDataRate(RF24_1MBPS);

  // CRC enabled by default in RF24 driver — protects against random noise acceptance.

  // Hardware retry timing + count: tuned per park noise; adjust empirically if needed.
  // RF24 encodes delay as (252 + 86*delay) µs between retries; count caps attempts.
  g_radio.setRetries(7, 15);

  // Fixed 32-byte FIFO width simplifies memcpy boundaries everywhere in firmware.
  g_radio.setPayloadSize(proto::PAYLOAD_MAX);

  // Dynamic payload widths add IRQ/FIFO complexity — we avoid them on ATmega8 builds.

  // Auto-ACK is the cornerstone of Shockburst reliability — keep it enabled unless an
  // exotic hub topology explicitly requires lossy broadcast (not this product).
  g_radio.setAutoAck(true);

  // Writing pipe — uplink toward backbone concentrator address.
  g_radio.openWritingPipe(node_cfg::ADDR_GW_NODE);

  // Reading pipe — unique per NODE_ID so backbone can target one cabinet safely.
  uint8_t rxaddr[5];
  node_cfg::node_listen_addr(rxaddr, node_cfg::kNodeId);
  g_radio.openReadingPipe(1, rxaddr);

  // Disable pipe 0 RX except when RF24 driver toggles internally — reduces stray accepts.
  g_radio.stopListening();
  g_radio.startListening();
  return true;
}

bool node_radio_send_frame(const uint8_t* data, uint8_t len) {
  if (!data || len == 0 || len > proto::PAYLOAD_MAX) return false;

  uint8_t tmp[proto::PAYLOAD_MAX];
  memcpy(tmp, data, len);
  pad_payload(tmp, len);

  g_radio.stopListening();

  bool ok = false;
  for (uint8_t attempt = 0; attempt <= NODE_RADIO_APP_TX_RETRIES; ++attempt) {
    ok = g_radio.write(tmp, proto::PAYLOAD_MAX);
    if (ok) break;

    // Post-hardware-retry cooldown with pseudo-random component (microseconds scale).
    const uint16_t r = prng16();
    delayMicroseconds(300U + (r & 0x3FFU));

    // Tiny millisecond-grade jitter as well — separates whole WRITE attempts from peers.
    delay(1U + (uint8_t)((r >> 8) & 0x07U));
  }

  g_radio.startListening();
  return ok;
}

bool node_radio_try_recv(uint8_t out[proto::PAYLOAD_MAX]) {
  if (!g_radio.available()) return false;
  g_radio.read(out, proto::PAYLOAD_MAX);
  return true;
}
