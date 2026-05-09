#pragma once

#include <stdint.h>

// Optional local overrides:
// - Copy `backbone_config.local.h.example` to `backbone_config.local.h`
// - Add `-DBACKBONE_CONFIG_LOCAL` to `build_flags` in platformio.ini
#ifdef BACKBONE_CONFIG_LOCAL
#include "backbone_config.local.h"
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

// Nodes send to this address.
#ifndef BACKBONE_RF_ADDR_GW_NODE_INIT
#define BACKBONE_RF_ADDR_GW_NODE_INIT 'G', 'W', 'A', 'Y', '1'
#endif

// Server sends to this address (separate pipe so we can distinguish traffic).
#ifndef BACKBONE_RF_ADDR_GW_SERVER_INIT
#define BACKBONE_RF_ADDR_GW_SERVER_INIT 'G', 'W', 'S', 'V', '1'
#endif

// Backbone writes to server using this address.
#ifndef BACKBONE_RF_ADDR_SERVER_INIT
#define BACKBONE_RF_ADDR_SERVER_INIT 'S', 'E', 'R', 'V', '1'
#endif

namespace cfg {

constexpr uint8_t RF_CE_PIN = BACKBONE_RF_CE_PIN;
constexpr uint8_t RF_CSN_PIN = BACKBONE_RF_CSN_PIN;
constexpr uint8_t RF_CHANNEL = BACKBONE_RF_CHANNEL;

extern const uint8_t ADDR_GW_NODE[5];
extern const uint8_t ADDR_GW_SERVER[5];
extern const uint8_t ADDR_SERVER[5];

}  // namespace cfg

