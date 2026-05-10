#pragma once
// =============================================================================
// backbone_config.h — RF pins, channel, and address bytes for gateway firmware
// =============================================================================
// Macros allow platformio.ini or a local header to override defaults without
// editing this file. See backbone_config.cpp for the extern array definitions.
// =============================================================================

#include <stdint.h>

// Optional untracked overrides: copy example to backbone_config.local.h and
// add -DBACKBONE_CONFIG_LOCAL to build_flags in platformio.ini.
#ifdef BACKBONE_CONFIG_LOCAL
#include "backbone_config.local.h"
#endif

// nRF24 chip enable and SPI chip-select pins (wiring-dependent).
#ifndef BACKBONE_RF_CE_PIN
#define BACKBONE_RF_CE_PIN 9
#endif
#ifndef BACKBONE_RF_CSN_PIN
#define BACKBONE_RF_CSN_PIN 10
#endif

// 2.4 GHz channel index; must match nodes and server sketch/hardware.
#ifndef BACKBONE_RF_CHANNEL
#define BACKBONE_RF_CHANNEL 100
#endif

// Nodes transmit toward this five-byte address (gateway RX pipe 1).
#ifndef BACKBONE_RF_ADDR_GW_NODE_INIT
#define BACKBONE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif

// Server transmits toward this address (gateway RX pipe 2) so pipe index
// disambiguates node-originated vs server-originated frames in software.
#ifndef BACKBONE_RF_ADDR_GW_SERVER_INIT
#define BACKBONE_RF_ADDR_GW_SERVER_INIT 'G', 'W', 'S', 'V', '1'
#endif

// Gateway opens this as its writing pipe when sending uplink to server radio.
#ifndef BACKBONE_RF_ADDR_SERVER_INIT
#define BACKBONE_RF_ADDR_SERVER_INIT 'S', 'E', 'R', 'V', '1'
#endif

namespace cfg {

constexpr uint8_t RF_CE_PIN = BACKBONE_RF_CE_PIN;
constexpr uint8_t RF_CSN_PIN = BACKBONE_RF_CSN_PIN;
constexpr uint8_t RF_CHANNEL = BACKBONE_RF_CHANNEL;

// Actual bytes live in backbone_config.cpp to satisfy ODR for const arrays.
extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace cfg
