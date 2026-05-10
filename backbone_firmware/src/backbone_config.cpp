// -----------------------------------------------------------------------------
// backbone_config.cpp — runtime storage for nRF24 five-byte addresses
// -----------------------------------------------------------------------------
// Headers expose `cfg::ADDR_*` as extern arrays; this TU defines them so the
// linker resolves symbols. Values come from macro init lists in
// backbone_config.h (override via backbone_config.local.h if used).
// -----------------------------------------------------------------------------

#include "backbone_config.h"  // BACKBONE_RF_ADDR_*_INIT macros and cfg namespace

namespace cfg {

// Address nodes use as their TX destination (gateway "node" pipe).
const uint8_t ADDR_GW_NODE[5] = {BACKBONE_RF_ADDR_GW_NODE_INIT};

// Address the server uses to reach the gateway on a separate RX pipe.
const uint8_t ADDR_GW_SERVER[5] = {BACKBONE_RF_ADDR_GW_SERVER_INIT};

// Address the backbone opens as default TX when sending toward the server.
const uint8_t ADDR_SERVER[5] = {BACKBONE_RF_ADDR_SERVER_INIT};

}  // namespace cfg
