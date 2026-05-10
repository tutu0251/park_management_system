#pragma once
// =============================================================================
// node_config.h — per-node identity, RF wiring, channel, gateway address
// =============================================================================
// Defaults match backbone_firmware settings. Override via build_flags in
// platformio.ini or optional node_config.local.h (see below).
// =============================================================================

#include <stdint.h>

// Untracked local overrides: define NODE_CONFIG_LOCAL and provide
// node_config.local.h for machine-specific constants without editing repo.
#ifdef NODE_CONFIG_LOCAL
#include "node_config.local.h"
#endif

// nRF24 CE/CSN pins (Arduino numbering); change if your shield uses other pins.
#ifndef NODE_RF_CE_PIN
#define NODE_RF_CE_PIN 9
#endif
#ifndef NODE_RF_CSN_PIN
#define NODE_RF_CSN_PIN 10
#endif

// RF channel index; must match backbone_config.h / server.
#ifndef NODE_RF_CHANNEL
#define NODE_RF_CHANNEL 100
#endif

// Five-byte address this node transmits to (gateway's "GWAY1" pipe).
// Must match BACKBONE_RF_ADDR_GW_NODE_INIT in backbone_firmware.
#ifndef NODE_RF_ADDR_GW_NODE_INIT
#define NODE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif

// Logical IDs embedded in SWIPE_REQ; backbone_nodes.h must list this node_id.
#ifndef NODE_ID
#define NODE_ID 1
#endif
#ifndef NODE_READER_ID
#define NODE_READER_ID 1
#endif
#ifndef NODE_DEFAULT_GAME_ID
#define NODE_DEFAULT_GAME_ID 1
#endif

// Background STATUS_PUSH cadence when status is degraded or changed (ms).
#ifndef NODE_STATUS_PUSH_MS
#define NODE_STATUS_PUSH_MS 60000UL
#endif

// Blocking wait in send_swipe() for PAY_RESP after successful TX (ms).
#ifndef NODE_PAY_WAIT_MS
#define NODE_PAY_WAIT_MS 1200UL
#endif

// After this many consecutive failed radio.write attempts, mark machine error.
#ifndef NODE_RADIO_FAIL_THRESHOLD
#define NODE_RADIO_FAIL_THRESHOLD 8
#endif

namespace node_cfg {

constexpr uint8_t RF_CE_PIN = NODE_RF_CE_PIN;
constexpr uint8_t RF_CSN_PIN = NODE_RF_CSN_PIN;
constexpr uint8_t RF_CHANNEL = NODE_RF_CHANNEL;

constexpr uint16_t kNodeId = NODE_ID;
constexpr uint16_t kReaderId = NODE_READER_ID;
constexpr uint16_t kDefaultGameId = NODE_DEFAULT_GAME_ID;

// Full gateway RX address bytes (const array used by RF24::openWritingPipe).
constexpr uint8_t ADDR_GW_NODE[5] = {NODE_RF_ADDR_GW_NODE_INIT};

// Builds this node's listening address: NODE1, NODE2, ... last byte is digit.
inline void node_listen_addr(uint8_t out[5], uint16_t node_id) {
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'D';
  out[3] = 'E';
  out[4] = static_cast<uint8_t>('0' + node_id);
}

}  // namespace node_cfg
