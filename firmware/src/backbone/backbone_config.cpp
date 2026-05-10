// =============================================================================
// backbone_config.cpp — storage for shared nRF24 five-byte address constants
// =============================================================================

#include "backbone_config.h"

namespace bbcfg {

const uint8_t ADDR_GW_NODE[5] = {BACKBONE_RF_ADDR_GW_NODE_INIT};
const uint8_t ADDR_GW_SERVER[5] = {BACKBONE_RF_ADDR_GW_SERVER_INIT};
const uint8_t ADDR_SERVER[5] = {BACKBONE_RF_ADDR_SERVER_INIT};

}  // namespace bbcfg
