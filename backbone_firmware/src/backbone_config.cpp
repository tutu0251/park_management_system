// -----------------------------------------------------------------------------
// backbone_config.cpp — storage for nRF24 five-byte addresses
// -----------------------------------------------------------------------------
// The header declares each ADDR_* as `extern const uint8_t[5]`; this TU
// provides the single definition required by ODR. Initializer macros come
// from backbone_config.h (overridable via backbone_config.local.h).
// -----------------------------------------------------------------------------

#include "backbone_config.h"

namespace bbcfg {

// Reader nodes write SWIPE_REQ / STATUS_PUSH / STATUS_RESP to this address.
const uint8_t ADDR_GW_NODE[5] = {BACKBONE_RF_ADDR_GW_NODE_INIT};

// Gateway writes downlink (PAY_RESP / CHECK_STATUS_REQ) to this address.
const uint8_t ADDR_GW_SERVER[5] = {BACKBONE_RF_ADDR_GW_SERVER_INIT};

// Backbone writes uplink toward the gateway using this address.
const uint8_t ADDR_SERVER[5] = {BACKBONE_RF_ADDR_SERVER_INIT};

}  // namespace bbcfg
