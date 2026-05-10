#pragma once
// =============================================================================
// node_config.h — compile-time deployment constants for the reader node
// =============================================================================
//
// HOW TO CUSTOMIZE
// ----------------
// Prefer platformio.ini `build_flags = -DNODE_ID=2 …` for per-environment tweaks.
// For private site overrides without editing tracked files: define NODE_CONFIG_LOCAL
// and supply node_config.local.h (untracked) with alternate pins/baud/macros.
//
// RF MUST MATCH backbone_firmware
// -------------------------------
// NODE_RF_CHANNEL — same 2.4 GHz channel index as backbone BACKBONE_RF_CHANNEL.
//
// NODE_RF_ADDR_GW_NODE_INIT — five initializer bytes for backbone's uplink listening
// address (logical "GWAY1"). Reader TX pipe targets this array verbatim.
//
// NODE_ID — logical machine id embedded in SWIPE_REQ AND the last byte of the RX
// listen address ('0' + NODE_ID). backbone_firmware/include/backbone_nodes.h must
// list the same node_id → NODE{n} mapping.
//
// USART / RFID
// ------------
// NODE_RFID_UART_BAUD — RFID reader UART speed on hardware Serial.
//
// NODE_RFID_LINE_MAX — max captured chars before newline; prevents RAM abuse if
// the reader streams garbage without LF.
//
// TIMING / RELIABILITY MACROS
// ---------------------------
// NODE_STATUS_PUSH_MS — how often loop() issues periodic STATUS_PUSH frames while
//                       rf_push_status succeeds (heartbeat for upstream dashboards).
//
// NODE_PAY_WAIT_MS — upper bound blocking wait for PAY_RESP after SWIPE_REQ ACK'd
//                    at ShockBurst layer (still subject to server latency).
//
// NODE_RADIO_FAIL_THRESHOLD — consecutive failed node_radio_send_frame attempts
//                             before elevating MachineStatus to STATUS_ERROR.
//
// NODE_RADIO_APP_TX_RETRIES — whole-packet retries inside node_radio_send_frame()
//                             after RF24's internal retry budget exhausts once.
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
/// Billing/category discriminator embedded into SWIPE_REQ (distinct from game_id tariff).
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
/// Worst case lines look like `SWIPE,` + up to 16 hex chars (8 credential bytes).
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
