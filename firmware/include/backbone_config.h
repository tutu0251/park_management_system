// Include guard — backbone headers may be pulled from multiple translation units.
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

// Sized integers for constexpr mirrors of preprocessor defaults.
#include <stdint.h>

#if defined(__AVR__)
// Watchdog timeout symbol used by backbone main.cpp when enabling WDT.
#include <avr/wdt.h>
#endif

#ifdef BACKBONE_CONFIG_LOCAL
// Optional overrides (pins, channel, thresholds) without editing repo defaults.
#include "backbone_config.local.h"
#endif

#ifndef BACKBONE_ID
// Printed in debug banner `BB<id>_ok` when BACKBONE_DEBUG_UART is enabled.
#define BACKBONE_ID 1
#endif

#ifndef BACKBONE_RF_CE_PIN
// RF24 chip-enable — must match backbone PCB wiring.
#define BACKBONE_RF_CE_PIN 9
#endif
#ifndef BACKBONE_RF_CSN_PIN
// RF24 SPI chip-select.
#define BACKBONE_RF_CSN_PIN 10
#endif

#ifndef BACKBONE_RF_CHANNEL
// 2.4 GHz channel index — must match NODE_RF_CHANNEL and GATEWAY_RF_CHANNEL.
#define BACKBONE_RF_CHANNEL 100
#endif

#ifndef BACKBONE_RF_PAYLOAD_SIZE
// Fixed Shockburst width shared with gateway/node (`proto::PAYLOAD_MAX`).
#define BACKBONE_RF_PAYLOAD_SIZE 32
#endif

#ifndef BACKBONE_RF_RETRIES_DELAY
// First argument to RF24::setRetries (delay units per Nordic data sheet).
#define BACKBONE_RF_RETRIES_DELAY 7
#endif
#ifndef BACKBONE_RF_RETRIES_ARC
// Second argument — auto-retry count before MCU sees failure.
#define BACKBONE_RF_RETRIES_ARC 15
#endif

#ifndef BACKBONE_APP_TX_RETRIES
// Software retry budget wrapped around each `radio.write` (plus backoff).
#define BACKBONE_APP_TX_RETRIES 3
#endif
#ifndef BACKBONE_TX_BACKOFF_MS_MAX
// Upper bound on random milliseconds between software retries.
#define BACKBONE_TX_BACKOFF_MS_MAX 20
#endif

#ifndef BACKBONE_RADIO_FAIL_THRESHOLD
// Consecutive TX failures before backbone_radio_recover() path arms.
#define BACKBONE_RADIO_FAIL_THRESHOLD 8
#endif

#if defined(__AVR__) && !defined(BACKBONE_WDTO)
// Default watchdog window when local config did not override.
#define BACKBONE_WDTO WDTO_2S
#endif

#ifndef BACKBONE_PENDING_DEPTH
// Reserved for legacy pending-queue sizing — referenced in router_tick for compile stability.
#define BACKBONE_PENDING_DEPTH 4
#endif
#ifndef BACKBONE_PENDING_TTL_MS
// Reserved TTL constant — future housekeeping may honor this millisecond horizon.
#define BACKBONE_PENDING_TTL_MS 1500UL
#endif

#ifndef BACKBONE_SERIAL_BAUD
// Debug UART speed when BACKBONE_DEBUG_UART routes statistics to Serial.
#define BACKBONE_SERIAL_BAUD 115200
#endif

#ifndef BACKBONE_DEBUG_UART
// Non-zero enables Serial statistics/banners in main.cpp; zero strips them for production flash savings.
#define BACKBONE_DEBUG_UART 1
#endif

#ifndef BACKBONE_RF_ADDR_GW_NODE_INIT
// Reader uplink sink — backbone RX pipe #1 subscribes to this five-byte pattern.
#define BACKBONE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif
#ifndef BACKBONE_RF_ADDR_GW_SERVER_INIT
// Gateway → backbone downlink — backbone RX pipe #2.
#define BACKBONE_RF_ADDR_GW_SERVER_INIT 'G', 'W', 'S', 'V', '1'
#endif
#ifndef BACKBONE_RF_ADDR_SERVER_INIT
// Backbone default TX toward gateway concentrator / PC path.
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

// Actual byte arrays defined once in backbone_config.cpp (ODR-safe).
extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace bbcfg
