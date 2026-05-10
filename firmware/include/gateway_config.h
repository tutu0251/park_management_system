// Include guard — this header is pulled from many gateway translation units.
#pragma once
// =============================================================================
// gateway_config.h — RF + UART constants for the USB gateway MCU
// =============================================================================
//
// Interlock with backbone_config.h / node_config.h:
//   GATEWAY_RF_CHANNEL / ADDR_* initializer macros must match byte-for-byte.
//
// DEFAULT PINS
// ------------
// Legacy gateway README used D2/D4 for CE/CSN on some shields — adjust here or via
// gateway_config.local.h if your PCB differs from reader/backbone wiring tables.
//
// =============================================================================

// Fixed-width integers for constexpr constants below.
#include <stdint.h>

#if defined(__AVR__)
// Watchdog timeout symbols — only meaningful on AVR builds of this sketch.
#include <avr/wdt.h>
#endif

#ifdef GATEWAY_CONFIG_LOCAL
// Optional board-specific overrides (pins, channel) without editing defaults here.
#include "gateway_config.local.h"
#endif

#if defined(__AVR__) && !defined(GATEWAY_WDTO)
// Default reset window if local config did not choose a different WDT period.
#define GATEWAY_WDTO WDTO_2S
#endif

#ifndef GATEWAY_ID
// Logical gateway instance printed in `GW<id>_ok` banner over UART.
#define GATEWAY_ID 1
#endif

#ifndef GATEWAY_RF_CE_PIN
// RF24 chip-enable pin — must match PCB wiring to the nRF24 module.
#define GATEWAY_RF_CE_PIN 2
#endif
#ifndef GATEWAY_RF_CSN_PIN
// SPI chip-select for RF24 — distinct from other SPI slaves if any.
#define GATEWAY_RF_CSN_PIN 4
#endif

#ifndef GATEWAY_RF_CHANNEL
// 2.4 GHz channel index shared with nodes and backbone concentrators.
#define GATEWAY_RF_CHANNEL 100
#endif

#ifndef GATEWAY_SERIAL_BAUD
// Host terminal speed — change together with PC scripts.
#define GATEWAY_SERIAL_BAUD 115200
#endif

#ifndef GATEWAY_RF_PAYLOAD_SIZE
// Shockburst payload width — keep 32 for compatibility across park_rf_protocol.h layouts.
#define GATEWAY_RF_PAYLOAD_SIZE 32
#endif

#ifndef GATEWAY_RF_RETRIES_DELAY
// RF24 `setRetries` delay argument — multiplied by RF timing units per Nordic docs.
#define GATEWAY_RF_RETRIES_DELAY 5
#endif
#ifndef GATEWAY_RF_RETRIES_ARC
// RF24 auto-retry count before Nordic declares ACK timeout to MCU.
#define GATEWAY_RF_RETRIES_ARC 15
#endif

#ifndef GATEWAY_APP_TX_RETRIES
// Software loop count wrapping each `radio.write` attempt — survives transient collisions.
#define GATEWAY_APP_TX_RETRIES 4
#endif
#ifndef GATEWAY_TX_BACKOFF_MS_MAX
// Upper bound for random `delay()` between software retries (milliseconds).
#define GATEWAY_TX_BACKOFF_MS_MAX 40
#endif

#ifndef GATEWAY_RF_ADDR_GW_NODE_INIT
// Five initializer bytes expanded into `gwcfg::ADDR_GW_NODE` — edit with mesh planner.
#define GATEWAY_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif
#ifndef GATEWAY_RF_ADDR_GW_SERVER_INIT
// Backbone listening pipe bytes for gateway-originated downlink frames.
#define GATEWAY_RF_ADDR_GW_SERVER_INIT 'G', 'W', 'S', 'V', '1'
#endif
#ifndef GATEWAY_RF_ADDR_SERVER_INIT
// Shared uplink concentrator address — gateway RX pipe subscribes here.
#define GATEWAY_RF_ADDR_SERVER_INIT 'S', 'E', 'R', 'V', '1'
#endif

namespace gwcfg {

constexpr uint8_t kGatewayId = GATEWAY_ID;
constexpr uint8_t kRfCePin = GATEWAY_RF_CE_PIN;
constexpr uint8_t kRfCsnPin = GATEWAY_RF_CSN_PIN;
constexpr uint8_t kRfChannel = GATEWAY_RF_CHANNEL;
constexpr uint32_t kSerialBaud = GATEWAY_SERIAL_BAUD;
constexpr uint8_t kPayloadMax = GATEWAY_RF_PAYLOAD_SIZE;
constexpr uint8_t kRfRetryDelay = GATEWAY_RF_RETRIES_DELAY;
constexpr uint8_t kRfRetryArc = GATEWAY_RF_RETRIES_ARC;
constexpr uint8_t kAppTxRetries = GATEWAY_APP_TX_RETRIES;
constexpr uint8_t kTxBackoffMsMax = GATEWAY_TX_BACKOFF_MS_MAX;

// Actual on-air bytes live in gateway_config.cpp — one definition for linker ODR safety.
extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace gwcfg
