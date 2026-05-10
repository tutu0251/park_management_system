#pragma once
// =============================================================================
// gateway_config.h — pins, RF timing, and nRF24 addresses (match backbone)
// =============================================================================
// Backbone sends uplink with openWritingPipe(ADDR_SERVER). This gateway must
// listen on that same five-byte address. PC-originated commands are transmitted
// toward ADDR_GW_SERVER so the backbone radio receives them on reading pipe 2.
// =============================================================================

#include <stdint.h>

#if defined(__AVR__)
#include <avr/wdt.h>
#endif

#if defined(__AVR__) && !defined(GATEWAY_WDTO)
#define GATEWAY_WDTO WDTO_2S
#endif

// Optional local overrides: create gateway_config.local.h and add
// -DGATEWAY_CONFIG_LOCAL to build_flags in platformio.ini.
#ifdef GATEWAY_CONFIG_LOCAL
#include "gateway_config.local.h"
#endif

#ifndef GATEWAY_ID
#define GATEWAY_ID 1
#endif

#ifndef GATEWAY_RF_CE_PIN
#define GATEWAY_RF_CE_PIN 2
#endif
#ifndef GATEWAY_RF_CSN_PIN
#define GATEWAY_RF_CSN_PIN 4
#endif

#ifndef GATEWAY_RF_CHANNEL
#define GATEWAY_RF_CHANNEL 100
#endif

#ifndef GATEWAY_SERIAL_BAUD
#define GATEWAY_SERIAL_BAUD 115200
#endif

#ifndef GATEWAY_RF_PAYLOAD_SIZE
#define GATEWAY_RF_PAYLOAD_SIZE 32
#endif

#ifndef GATEWAY_RF_RETRIES_ARC
#define GATEWAY_RF_RETRIES_ARC 5
#endif
#ifndef GATEWAY_RF_RETRIES_DELAY
#define GATEWAY_RF_RETRIES_DELAY 15
#endif

// Application-level TX attempts after RF24 auto-retry exhausts (each attempt
// includes random backoff before calling write again).
#ifndef GATEWAY_APP_TX_RETRIES
#define GATEWAY_APP_TX_RETRIES 4
#endif
#ifndef GATEWAY_TX_BACKOFF_MS_MAX
#define GATEWAY_TX_BACKOFF_MS_MAX 40
#endif

// --- nRF24 five-byte addresses (defaults mirror backbone_config.h) -----------

#ifndef GATEWAY_RF_ADDR_GW_NODE_INIT
#define GATEWAY_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif
#ifndef GATEWAY_RF_ADDR_GW_SERVER_INIT
#define GATEWAY_RF_ADDR_GW_SERVER_INIT 'G', 'W', 'S', 'V', '1'
#endif
#ifndef GATEWAY_RF_ADDR_SERVER_INIT
#define GATEWAY_RF_ADDR_SERVER_INIT 'S', 'E', 'R', 'V', '1'
#endif

namespace gwcfg {

constexpr uint8_t kGatewayId = GATEWAY_ID;
constexpr uint8_t kRfCePin = GATEWAY_RF_CE_PIN;
constexpr uint8_t kRfCsnPin = GATEWAY_RF_CSN_PIN;
constexpr uint8_t kRfChannel = GATEWAY_RF_CHANNEL;
constexpr uint32_t kSerialBaud = GATEWAY_SERIAL_BAUD;
constexpr uint8_t kPayloadMax = GATEWAY_RF_PAYLOAD_SIZE;
constexpr uint8_t kRfRetryArc = GATEWAY_RF_RETRIES_ARC;
constexpr uint8_t kRfRetryDelay = GATEWAY_RF_RETRIES_DELAY;
constexpr uint8_t kAppTxRetries = GATEWAY_APP_TX_RETRIES;
constexpr uint8_t kTxBackoffMsMax = GATEWAY_TX_BACKOFF_MS_MAX;

extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace gwcfg
