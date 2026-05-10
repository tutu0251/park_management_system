#pragma once
// =============================================================================
// node_config.h — compile-time wiring / timing policy for the reader node
// =============================================================================
//
// PURPOSE
// -------
// Centralizes every macro the firmware uses so deployments differ only by
// platformio.ini build_flags or an optional untracked node_config.local.h.
//
// RF COMPATIBILITY CHECKLIST (must align with backbone_firmware)
// --------------------------------------------------------------
// - NODE_RF_CHANNEL           ↔ backbone BACKBONE_RF_CHANNEL (same 2.4 GHz index).
// - NODE_RF_ADDR_GW_NODE_INIT ↔ backbone BACKBONE_RF_ADDR_GW_NODE_INIT ("GWAY1").
// - NODE_ID                   ↔ backbone KNOWN_NODES[].node_id and pipe address
//                               suffix ('1'..'9') produced by node_listen_addr().
//
// USART STRATEGY ON ATmega8
// -------------------------
// One hardware serial — reserved for the game machine at NODE_MACHINE_UART_BAUD.
// RFID UART modules default to SoftwareSerial pins NODE_RFID_SOFT_* .
//
// LOCAL OVERRIDES
// ---------------
// Define NODE_CONFIG_LOCAL and add node_config.local.h for site-specific pins/Baud
// without dirtying the tracked defaults below.
//
// =============================================================================

#include <stdint.h>

#ifdef NODE_CONFIG_LOCAL
#include "node_config.local.h"
#endif

// nRF24 control pins (Arduino-style numbering for MiniCore ATmega8 boards).
#ifndef NODE_RF_CE_PIN
#define NODE_RF_CE_PIN 9
#endif
#ifndef NODE_RF_CSN_PIN
#define NODE_RF_CSN_PIN 10
#endif

#ifndef NODE_RF_CHANNEL
#define NODE_RF_CHANNEL 100
#endif

// Five-byte TX destination toward backbone's NODE-collection pipe.
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

#ifndef NODE_STATUS_PUSH_MS
#define NODE_STATUS_PUSH_MS 60000UL
#endif
#ifndef NODE_PAY_WAIT_MS
#define NODE_PAY_WAIT_MS 1200UL
#endif
#ifndef NODE_RADIO_FAIL_THRESHOLD
#define NODE_RADIO_FAIL_THRESHOLD 8
#endif

// After ShockBurst auto-retries fail, how many times node_radio_send_frame()
// re-attempts the whole 32-byte write with pseudo-random spacing.
#ifndef NODE_RADIO_APP_TX_RETRIES
#define NODE_RADIO_APP_TX_RETRIES 3
#endif

#ifndef NODE_MACHINE_UART_BAUD
#define NODE_MACHINE_UART_BAUD 9600UL
#endif

// RFID SoftwareSerial: MCU listens on SOFT_RX; SOFT_TX_DUMMY unused electrically.
#ifndef NODE_RFID_SOFT_RX_PIN
#define NODE_RFID_SOFT_RX_PIN 4
#endif
#ifndef NODE_RFID_SOFT_TX_DUMMY_PIN
#define NODE_RFID_SOFT_TX_DUMMY_PIN 5
#endif
#ifndef NODE_RFID_UART_BAUD
#define NODE_RFID_UART_BAUD 9600UL
#endif

#ifndef NODE_RFID_LINE_MAX
#define NODE_RFID_LINE_MAX 16
#endif

namespace node_cfg {

constexpr uint8_t RF_CE_PIN = NODE_RF_CE_PIN;
constexpr uint8_t RF_CSN_PIN = NODE_RF_CSN_PIN;
constexpr uint8_t RF_CHANNEL = NODE_RF_CHANNEL;

constexpr uint16_t kNodeId = NODE_ID;
constexpr uint16_t kReaderId = NODE_READER_ID;
constexpr uint16_t kDefaultGameId = NODE_DEFAULT_GAME_ID;

constexpr uint8_t ADDR_GW_NODE[5] = {NODE_RF_ADDR_GW_NODE_INIT};

// Builds per-node listen address "NODE{n}"; backbone table must match bytes exactly.
inline void node_listen_addr(uint8_t out[5], uint16_t node_id) {
  out[0] = 'N';
  out[1] = 'O';
  out[2] = 'D';
  out[3] = 'E';
  out[4] = static_cast<uint8_t>('0' + node_id);
}

}  // namespace node_cfg
