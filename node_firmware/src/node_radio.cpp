// -----------------------------------------------------------------------------
// node_radio.cpp — nRF24L01+ bring-up and framed TX/RX for reader ↔ backbone
// -----------------------------------------------------------------------------
//
// ADDRESSING MODEL (must match backbone_firmware + server sketches)
// -----------------------------------------------------------------
// - Writing pipe (TX): node_cfg::ADDR_GW_NODE → default backbone RX pipe that
//   collects reader uplinks (logical name "GWAY1" in macros).
// - Reading pipe 1 (RX): unique per reader — node_cfg::node_listen_addr(...):
//   ASCII bytes 'N','O','D','E', plus '0'+node_id (supports ids 1..9 out of box).
//
// FRAME WIDTH
// -----------
// nRF24 ShockBurst payloads are configured as fixed 32 bytes (proto::PAYLOAD_MAX).
// Short logical messages (e.g. 2-byte STATUS_PUSH) are copied then zero-padded so
// CRC and backbone parsers always see a full buffer — mirrors gateway-side memcpy.
//
// RELIABILITY LAYERS
// ------------------
// 1) Hardware auto-ACK + RF24::setRetries(delay_count, retry_count) — handles
//    transient collisions at the ShockBurst layer.
// 2) Application retries in node_radio_send_frame(): NODE_RADIO_APP_TX_RETRIES
//    attempts with pseudo-random microsecond backoff between attempts to reduce
//    synchronized repeated collisions when many nodes contend.
//
// SRAM NOTE
// ---------
// RF24 owns internal driver buffers; we keep only one static TX staging frame
// here (32 bytes) plus the RF24 object itself — tuned for ATmega8's 1 KiB RAM.
//
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <string.h>

#include "node_config.h"
#include "node_radio.h"

static RF24 g_radio(node_cfg::RF_CE_PIN, node_cfg::RF_CSN_PIN);
static uint8_t g_node_listen[5];

// Tiny LFSR-style mix — not cryptographic; only spreads retry jitter in time.
static uint16_t prng16() {
  static uint16_t s = 0xACE1u;
  uint16_t x = s;
  x ^= static_cast<uint16_t>(x << 7);
  x ^= static_cast<uint16_t>(x >> 9);
  x ^= static_cast<uint16_t>(x << 8);
  s = x;
  return x;
}

bool node_radio_begin() {
  if (!g_radio.begin()) return false;

  // RF band index (not MHz directly); must match backbone + billing peer radios.
  g_radio.setChannel(node_cfg::RF_CHANNEL);
  g_radio.setPALevel(RF24_PA_HIGH);
  g_radio.setDataRate(RF24_1MBPS);
  // Auto-retry timing: (delay+1)*250µs between retries, retry_count+1 attempts.
  g_radio.setRetries(5, 15);
  g_radio.setCRCLength(RF24_CRC_16);
  g_radio.setPayloadSize(proto::PAYLOAD_MAX);
  g_radio.setAutoAck(true);

  node_cfg::node_listen_addr(g_node_listen, node_cfg::kNodeId);
  // Pipe 0 is often reserved for TX addressing on nRF24; pipe 1 is a typical choice
  // for first RX address with RF24 Arduino wrapper defaults used elsewhere in repo.
  g_radio.openReadingPipe(1, g_node_listen);
  g_radio.openWritingPipe(node_cfg::ADDR_GW_NODE);
  g_radio.startListening();
  return true;
}

bool node_radio_send_frame(const uint8_t* payload, uint8_t len) {
  // Static staging avoids a large stack allocation in callers on tiny SRAM MCUs.
  static uint8_t frame[proto::PAYLOAD_MAX];

  if (!payload || len == 0 || len > proto::PAYLOAD_MAX) return false;
  memcpy(frame, payload, len);
  if (len < proto::PAYLOAD_MAX) memset(frame + len, 0, proto::PAYLOAD_MAX - len);

  for (uint8_t attempt = 0; attempt < NODE_RADIO_APP_TX_RETRIES; ++attempt) {
    // First attempt starts immediately; later attempts wait pseudo-random µs.
    if (attempt) {
      const uint16_t r = prng16();
      delayMicroseconds(250U + (r & 0x7FFU));
    }

    // RF24 requires stopListening() before write(); restore RX afterward so we do
    // not miss downlink PAY_RESP / polls during sustained TX bursts.
    g_radio.stopListening();
    g_radio.openWritingPipe(node_cfg::ADDR_GW_NODE);
    const bool ok = g_radio.write(frame, proto::PAYLOAD_MAX);
    g_radio.startListening();
    if (ok) return true;
  }
  return false;
}

bool node_radio_try_recv(uint8_t out32[proto::PAYLOAD_MAX]) {
  if (!g_radio.available()) return false;
  g_radio.read(out32, proto::PAYLOAD_MAX);
  return true;
}
