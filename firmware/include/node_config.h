// Include guard.
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
// RF24 CE pin for reader board wiring.
#define NODE_RF_CE_PIN 9
#endif
#ifndef NODE_RF_CSN_PIN
// RF24 SPI chip-select.
#define NODE_RF_CSN_PIN 10
#endif

#ifndef NODE_RF_CHANNEL
// Must equal backbone/gateway channel for packets to meet on-air.
#define NODE_RF_CHANNEL 100
#endif

#ifndef NODE_RF_ADDR_GW_NODE_INIT
// Bytes expanded into ADDR_GW_NODE — backbone listens on matching pipe.
#define NODE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif

#ifndef NODE_ID
// Encoded into SWIPE_REQ and NODE{n} RX pipe — set per cabinet via build_flags.
#define NODE_ID 1
#endif
#ifndef NODE_READER_ID
// Logical terminal face when multiple readers share hardware.
#define NODE_READER_ID 1
#endif
#ifndef NODE_DEFAULT_GAME_ID
// Tariff bucket placed into SWIPE_REQ until UART carries richer context.
#define NODE_DEFAULT_GAME_ID 1
#endif

#ifndef NODE_DEFAULT_TYPE_ID
// Compact category byte mirrored into RF structs.
#define NODE_DEFAULT_TYPE_ID 1
#endif

#ifndef NODE_STATUS_PUSH_MS
// Baseline interval between STATUS_PUSH frames — jitter added in main.cpp.
#define NODE_STATUS_PUSH_MS 60000UL
#endif
#ifndef NODE_PAY_WAIT_MS
// Upper bound blocking window while polling RF during payment authorization.
#define NODE_PAY_WAIT_MS 1200UL
#endif
#ifndef NODE_RADIO_FAIL_THRESHOLD
// Failed TX streak before MachineStatus degrades to ERROR.
#define NODE_RADIO_FAIL_THRESHOLD 8
#endif

#ifndef NODE_RADIO_APP_TX_RETRIES
// Extra write attempts after Nordic auto-retry — spreads collisions across cabinets.
#define NODE_RADIO_APP_TX_RETRIES 3
#endif

#ifndef NODE_RFID_UART_BAUD
// Payment terminal default serial speed.
#define NODE_RFID_UART_BAUD 9600UL
#endif

#ifndef NODE_RFID_LINE_MAX
// Static line assembler cap — protects SRAM from hostile UART streams.
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

// Compile-time TX pipe destination — must match backbone ADDR_GW_NODE bytes.
constexpr uint8_t ADDR_GW_NODE[5] = {NODE_RF_ADDR_GW_NODE_INIT};

inline void node_listen_addr(uint8_t out[5], uint16_t node_id) {
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'D';
  out[3] = 'E';
  // Single-digit cabinet ID — must stay consistent with backbone_router node_id routing.
  out[4] = static_cast<uint8_t>('0' + node_id);
}

}  // namespace node_cfg
