#pragma once
// =============================================================================
// node_config.h — compile-time deployment constants for the reader NODE image
// =============================================================================
//
// Prefer platformio.ini build_flags (-DNODE_ID=…) for per-machine identity.
// Optional private overrides: define NODE_CONFIG_LOCAL and add node_config.local.h.
//
// RF INTERLOCK (must match backbone_config.h / gateway_config.h)
// ---------------------------------------------------------------
// NODE_RF_CHANNEL           ↔ BACKBONE_RF_CHANNEL ↔ GATEWAY_RF_CHANNEL
// NODE_RF_ADDR_GW_NODE_INIT ↔ BACKBONE_RF_ADDR_GW_NODE_INIT (logical “GWAY1” uplink sink)
// NODE_ID                   — encoded in SWIPE_REQ and in per-node RX pipe (“NODE{n}”).
//
// =============================================================================

#include <stdint.h>

#ifdef NODE_CONFIG_LOCAL
#include "node_config.local.h"
#endif

#ifndef NODE_RF_CE_PIN
#define NODE_RF_CE_PIN 9
#endif
#ifndef NODE_RF_CSN_PIN
#define NODE_RF_CSN_PIN 10
#endif

#ifndef NODE_RF_CHANNEL
#define NODE_RF_CHANNEL 100
#endif

#ifndef NODE_RF_ADDR_GW_NODE_INIT
#define NODE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif

#ifndef NODE_ID
#define NODE_ID 1
#endif
#ifndef NODE_READER_ID
#define NODE_READER_ID 1
#endif
#ifndef NODE_DEFAULT_GAME_ID
#define NODE_DEFAULT_GAME_ID 1
#endif

#ifndef NODE_DEFAULT_TYPE_ID
#define NODE_DEFAULT_TYPE_ID 1
#endif

#ifndef NODE_STATUS_PUSH_MS
#define NODE_STATUS_PUSH_MS 60000UL
#endif
#ifndef NODE_PAY_WAIT_MS
#define NODE_PAY_WAIT_MS 1200UL
#endif
#ifndef NODE_RADIO_FAIL_THRESHOLD
#define NODE_RADIO_FAIL_THRESHOLD 8
#endif

#ifndef NODE_RADIO_APP_TX_RETRIES
#define NODE_RADIO_APP_TX_RETRIES 3
#endif

#ifndef NODE_RFID_UART_BAUD
#define NODE_RFID_UART_BAUD 9600UL
#endif

#ifndef NODE_RFID_LINE_MAX
#define NODE_RFID_LINE_MAX 40
#endif

namespace node_cfg {

constexpr uint8_t RF_CE_PIN = NODE_RF_CE_PIN;
constexpr uint8_t RF_CSN_PIN = NODE_RF_CSN_PIN;
constexpr uint8_t RF_CHANNEL = NODE_RF_CHANNEL;

constexpr uint16_t kNodeId = NODE_ID;
constexpr uint16_t kReaderId = NODE_READER_ID;
constexpr uint16_t kDefaultGameId = NODE_DEFAULT_GAME_ID;

constexpr uint8_t kDefaultTypeId = NODE_DEFAULT_TYPE_ID;

constexpr uint8_t ADDR_GW_NODE[5] = {NODE_RF_ADDR_GW_NODE_INIT};

inline void node_listen_addr(uint8_t out[5], uint16_t node_id) {
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'D';
  out[3] = 'E';
  out[4] = static_cast<uint8_t>('0' + node_id);
}

}  // namespace node_cfg
