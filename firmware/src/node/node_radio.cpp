// =============================================================================
// node_radio.cpp — RF24 setup aligned with backbone/gateway timing defaults
// =============================================================================
//
// TRANSPORT POLICY (why these knobs matter)
// -----------------------------------------
//   • Fixed 32-byte FIFO width everywhere — matches proto::PAYLOAD_MAX and eliminates
//     variable-length IRQ complexity on ATmega8 class MCUs.
//   • Auto-ACK enabled — Shockburst confirms delivery into the peer radio’s FIFO; without
//     auto-ACK we would need application-level ACK packets (double airtime + flash cost).
//   • Hardware retries (`RF24::setRetries`) absorb brief Wi-Fi/microwave bursts before we
//     escalate to whole-frame application retries.
//   • `NODE_RADIO_APP_TX_RETRIES` adds **additional** write attempts with pseudo-random
//     spacing so two cabinets that collide do not forever re-collide on identical timers.
//
// ADDRESSING MODEL (must match backbone_config.h / gateway_config.h byte-for-byte)
// --------------------------------------------------------------------------------
//   TX pipe destination : ADDR_GW_NODE (“GWAY1”) — backbone RX pipe #1 collects uplink.
//   RX pipe 1 address   : ASCII pattern NODE + ASCII digit of NODE_ID — backbone opens a
//                         dedicated writing pipe per cabinet when sending PAY_RESP, polls, etc.
//
// PRNG NOTE
// ---------
// `prng16()` is **not** cryptographic noise — only millisecond-scale jitter for retries.
//
// =============================================================================

#include "node_radio.h"

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#include <string.h>

#include "node_config.h"

static RF24 g_radio(node_cfg::RF_CE_PIN, node_cfg::RF_CSN_PIN);

static uint16_t g_prng = 0xBEEF;

uint16_t prng16(void) {
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

  g_radio.setChannel(node_cfg::RF_CHANNEL);
  g_radio.setPALevel(RF24_PA_LOW);
  g_radio.setDataRate(RF24_1MBPS);
  g_radio.setRetries(7, 15);
  g_radio.setPayloadSize(proto::PAYLOAD_MAX);
  g_radio.setAutoAck(true);

  g_radio.openWritingPipe(node_cfg::ADDR_GW_NODE);

  uint8_t rxaddr[5];
  node_cfg::node_listen_addr(rxaddr, node_cfg::kNodeId);
  g_radio.openReadingPipe(1, rxaddr);

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

    const uint16_t r = prng16();
    delayMicroseconds(300U + (r & 0x3FFU));
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
