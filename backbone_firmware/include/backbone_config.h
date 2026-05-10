#pragma once
// =============================================================================
// backbone_config.h — pins, RF channel, addresses, WDT for the ATmega8 backbone
// =============================================================================
//
// PURPOSE
// -------
// Centralizes every macro the backbone firmware uses so deployments differ only
// by platformio.ini build_flags or an optional untracked backbone_config.local.h.
//
// COMPATIBILITY CHECKLIST (must agree across the three firmwares)
// ---------------------------------------------------------------
// - BACKBONE_RF_CHANNEL          ↔ NODE_RF_CHANNEL ↔ GATEWAY_RF_CHANNEL.
// - BACKBONE_RF_ADDR_GW_NODE_INIT ↔ NODE_RF_ADDR_GW_NODE_INIT ("GWAY1").
//     Reader nodes write to this address; backbone listens on RX pipe 1.
// - BACKBONE_RF_ADDR_GW_SERVER_INIT ↔ GATEWAY_RF_ADDR_GW_SERVER_INIT ("GWSV1").
//     Gateway writes downlink to this address; backbone listens on RX pipe 2.
// - BACKBONE_RF_ADDR_SERVER_INIT  ↔ GATEWAY_RF_ADDR_SERVER_INIT ("SERV1").
//     Backbone writes uplink to this address; gateway listens on RX pipe 1.
// - Reader-node listen address pattern "NODE{n}" is mirrored from
//   node_cfg::node_listen_addr() in node_firmware/include/node_config.h.
//
// ATmega8 NOTES
// -------------
// - Single hardware USART: used here for OPTIONAL relay debug only (no game
//   machine attached). Disable via -DBACKBONE_DEBUG_UART=0 to save SRAM/flash.
// - Hardware SPI pins (PB3/PB4/PB5) are managed by the RF24/SPI libraries.
//   CE / CSN may be any free digital pin — defaults D9 / D10 mirror node_firmware.
// - WDTO_2S is the largest reliable watchdog window on ATmega8.
//
// =============================================================================

#include <stdint.h>

#if defined(__AVR__)
#include <avr/wdt.h>
#endif

// Optional untracked overrides: copy backbone_config.local.h.example to
// backbone_config.local.h and add -DBACKBONE_CONFIG_LOCAL to build_flags.
#ifdef BACKBONE_CONFIG_LOCAL
#include "backbone_config.local.h"
#endif

// --- Identity ----------------------------------------------------------------

#ifndef BACKBONE_ID
#define BACKBONE_ID 1
#endif

// --- nRF24 wiring ------------------------------------------------------------

#ifndef BACKBONE_RF_CE_PIN
#define BACKBONE_RF_CE_PIN 9
#endif
#ifndef BACKBONE_RF_CSN_PIN
#define BACKBONE_RF_CSN_PIN 10
#endif

// --- nRF24 RF parameters -----------------------------------------------------

#ifndef BACKBONE_RF_CHANNEL
#define BACKBONE_RF_CHANNEL 100
#endif

#ifndef BACKBONE_RF_PAYLOAD_SIZE
#define BACKBONE_RF_PAYLOAD_SIZE 32
#endif

// nRF24 ShockBurst auto-retry: (delay+1)*250µs between attempts, attempts+1.
#ifndef BACKBONE_RF_RETRIES_DELAY
#define BACKBONE_RF_RETRIES_DELAY 5
#endif
#ifndef BACKBONE_RF_RETRIES_ARC
#define BACKBONE_RF_RETRIES_ARC 15
#endif

// Application TX layer (after nRF24 hardware retries exhaust).
#ifndef BACKBONE_APP_TX_RETRIES
#define BACKBONE_APP_TX_RETRIES 3
#endif
#ifndef BACKBONE_TX_BACKOFF_MS_MAX
#define BACKBONE_TX_BACKOFF_MS_MAX 20
#endif

// Consecutive TX failures (any direction) before radio recover() fires.
#ifndef BACKBONE_RADIO_FAIL_THRESHOLD
#define BACKBONE_RADIO_FAIL_THRESHOLD 8
#endif

// --- Watchdog ----------------------------------------------------------------

#if defined(__AVR__) && !defined(BACKBONE_WDTO)
// ATmega8 supports up to WDTO_2S — enough for one loop pass plus retries.
#define BACKBONE_WDTO WDTO_2S
#endif

// --- Pending swipe correlation ----------------------------------------------

// PAY_RESP carries no transaction_id; the backbone uses a FIFO ring of recent
// SWIPE_REQ uplinks to know which reader address to forward each PAY_RESP to.
#ifndef BACKBONE_PENDING_DEPTH
#define BACKBONE_PENDING_DEPTH 4
#endif
#ifndef BACKBONE_PENDING_TTL_MS
#define BACKBONE_PENDING_TTL_MS 1500UL
#endif

// --- Optional UART debug -----------------------------------------------------

#ifndef BACKBONE_SERIAL_BAUD
#define BACKBONE_SERIAL_BAUD 115200
#endif

// 1 = compile in debug Serial output; 0 = remove all Serial.* calls.
#ifndef BACKBONE_DEBUG_UART
#define BACKBONE_DEBUG_UART 1
#endif

// --- nRF24 five-byte addresses (defaults shared with node + gateway) ---------

#ifndef BACKBONE_RF_ADDR_GW_NODE_INIT
#define BACKBONE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif
#ifndef BACKBONE_RF_ADDR_GW_SERVER_INIT
#define BACKBONE_RF_ADDR_GW_SERVER_INIT 'G', 'W', 'S', 'V', '1'
#endif
#ifndef BACKBONE_RF_ADDR_SERVER_INIT
#define BACKBONE_RF_ADDR_SERVER_INIT 'S', 'E', 'R', 'V', '1'
#endif

namespace bbcfg {

constexpr uint8_t kBackboneId = BACKBONE_ID;
constexpr uint8_t kRfCePin = BACKBONE_RF_CE_PIN;
constexpr uint8_t kRfCsnPin = BACKBONE_RF_CSN_PIN;
constexpr uint8_t kRfChannel = BACKBONE_RF_CHANNEL;
constexpr uint8_t kPayloadMax = BACKBONE_RF_PAYLOAD_SIZE;
constexpr uint8_t kRfRetryDelay = BACKBONE_RF_RETRIES_DELAY;
constexpr uint8_t kRfRetryArc = BACKBONE_RF_RETRIES_ARC;
constexpr uint8_t kAppTxRetries = BACKBONE_APP_TX_RETRIES;
constexpr uint8_t kTxBackoffMsMax = BACKBONE_TX_BACKOFF_MS_MAX;
constexpr uint8_t kRadioFailThreshold = BACKBONE_RADIO_FAIL_THRESHOLD;
constexpr uint8_t kPendingDepth = BACKBONE_PENDING_DEPTH;
constexpr uint32_t kPendingTtlMs = BACKBONE_PENDING_TTL_MS;
constexpr uint32_t kSerialBaud = BACKBONE_SERIAL_BAUD;

// Defined in backbone_config.cpp (one storage location for the linker).
extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace bbcfg
