#pragma once
// =============================================================================
// backbone_config.h — pins, RF timing, watchdog (must agree with node + gateway)
// =============================================================================
//
// ADDRESSING RECAP
// ----------------
// ADDR_GW_NODE   — readers TX here; backbone RX pipe #1.
// ADDR_GW_SERVER — gateway TX here; backbone RX pipe #2 (PC-originated commands).
// ADDR_SERVER    — backbone TX here; gateway RX pipe #1 (park uplink toward PC).
//
// OPTIONAL UART
// -------------
// BACKBONE_DEBUG_UART enables lightweight statistics prints for bench bring-up.
// Set to 0 in production to reclaim flash/SRAM on ATmega8-class boards.
//
// =============================================================================

#include <stdint.h>

#if defined(__AVR__)
#include <avr/wdt.h>
#endif

#ifdef BACKBONE_CONFIG_LOCAL
#include "backbone_config.local.h"
#endif

#ifndef BACKBONE_ID
#define BACKBONE_ID 1
#endif

#ifndef BACKBONE_RF_CE_PIN
#define BACKBONE_RF_CE_PIN 9
#endif
#ifndef BACKBONE_RF_CSN_PIN
#define BACKBONE_RF_CSN_PIN 10
#endif

#ifndef BACKBONE_RF_CHANNEL
#define BACKBONE_RF_CHANNEL 100
#endif

#ifndef BACKBONE_RF_PAYLOAD_SIZE
#define BACKBONE_RF_PAYLOAD_SIZE 32
#endif

#ifndef BACKBONE_RF_RETRIES_DELAY
#define BACKBONE_RF_RETRIES_DELAY 7
#endif
#ifndef BACKBONE_RF_RETRIES_ARC
#define BACKBONE_RF_RETRIES_ARC 15
#endif

#ifndef BACKBONE_APP_TX_RETRIES
#define BACKBONE_APP_TX_RETRIES 3
#endif
#ifndef BACKBONE_TX_BACKOFF_MS_MAX
#define BACKBONE_TX_BACKOFF_MS_MAX 20
#endif

#ifndef BACKBONE_RADIO_FAIL_THRESHOLD
#define BACKBONE_RADIO_FAIL_THRESHOLD 8
#endif

#if defined(__AVR__) && !defined(BACKBONE_WDTO)
#define BACKBONE_WDTO WDTO_2S
#endif

#ifndef BACKBONE_PENDING_DEPTH
#define BACKBONE_PENDING_DEPTH 4
#endif
#ifndef BACKBONE_PENDING_TTL_MS
#define BACKBONE_PENDING_TTL_MS 1500UL
#endif

#ifndef BACKBONE_SERIAL_BAUD
#define BACKBONE_SERIAL_BAUD 115200
#endif

#ifndef BACKBONE_DEBUG_UART
#define BACKBONE_DEBUG_UART 1
#endif

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

extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace bbcfg
