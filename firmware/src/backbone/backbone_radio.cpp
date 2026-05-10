// =============================================================================
// backbone_radio.cpp — dual RX pipes (readers + gateway) with one TX scheduler
// =============================================================================
//
// Pipe map (must mirror gateway_radio.cpp wiring):
//   RX pipe 1 — ADDR_GW_NODE   (reader uplink).
//   RX pipe 2 — ADDR_GW_SERVER (gateway / PC downlink).
//
// Writes temporarily stop listening, select either ADDR_SERVER (toward PC path) or the
// per-node “NODE{n}” pipe toward a cabinet radio, then return to RX mode.
//
// RF24::setRetries(delay, count) — parameter order is (ARD step, retry count); several
// early gateway drafts swapped these; keep delay first here for predictable airtime.
//
// =============================================================================

#include "backbone_radio.h"

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <string.h>

#include "backbone_config.h"

namespace {

RF24 g_radio(bbcfg::kRfCePin, bbcfg::kRfCsnPin);

bool apply_common_rf_settings(void) {
  if (!g_radio.begin()) return false;
  g_radio.setChannel(bbcfg::kRfChannel);
  g_radio.setPALevel(RF24_PA_LOW);
  g_radio.setDataRate(RF24_1MBPS);
  g_radio.setRetries(bbcfg::kRfRetryDelay, bbcfg::kRfRetryArc);
  g_radio.setPayloadSize(bbcfg::kPayloadMax);
  g_radio.setAutoAck(true);
  return true;
}

static void reader_listen_addr(uint8_t out[5], uint16_t node_id) {
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'D';
  out[3] = 'E';
  out[4] = static_cast<uint8_t>('0' + node_id);
}

// Shared TX primitive — stops RX briefly (required by RF24), sends padded 32-byte frame.
static bool tx_blocking(const uint8_t dest[5], const uint8_t frame[32]) {
  if (!dest || !frame) return false;

  for (uint8_t attempt = 0; attempt <= bbcfg::kAppTxRetries; ++attempt) {
    if (attempt > 0) {
      const uint8_t ms = 1 + static_cast<uint8_t>(random() % bbcfg::kTxBackoffMsMax);
      delay(ms);
    }

    g_radio.stopListening();
    g_radio.openWritingPipe(dest);
    const bool ok = g_radio.write(frame, bbcfg::kPayloadMax);
    g_radio.startListening();
    if (ok) return true;

    g_radio.flush_tx();
  }
  return false;
}

}  // namespace

bool backbone_radio_begin(void) {
  SPI.begin();
  if (!apply_common_rf_settings()) return false;

  g_radio.openReadingPipe(1, bbcfg::ADDR_GW_NODE);
  g_radio.openReadingPipe(2, bbcfg::ADDR_GW_SERVER);
  g_radio.openWritingPipe(bbcfg::ADDR_SERVER);

  g_radio.startListening();
  return true;
}

bool backbone_radio_try_recv(uint8_t out[32], uint8_t* pipe_out) {
  if (!out) return false;
  uint8_t pipe = 0;
  if (!g_radio.available(&pipe)) return false;
  g_radio.read(out, bbcfg::kPayloadMax);
  if (pipe_out) *pipe_out = pipe;
  return true;
}

bool backbone_radio_send_to_server(const uint8_t frame[32]) {
  return tx_blocking(bbcfg::ADDR_SERVER, frame);
}

bool backbone_radio_send_to_reader(uint16_t node_id, const uint8_t frame[32]) {
  if (node_id == 0 || node_id > 9) return false;
  uint8_t addr[5];
  reader_listen_addr(addr, node_id);
  return tx_blocking(addr, frame);
}

void backbone_radio_recover(void) {
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
