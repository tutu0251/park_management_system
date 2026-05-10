// =============================================================================
// backbone_config.cpp — storage for shared nRF24 five-byte address constants
// =============================================================================

// Declarations + initializer macros for the three backbone RF addresses.
#include "backbone_config.h"

namespace bbcfg {

// Reader uplink pipe address — matches gateway/node expectations (`GWAY1`-style bytes).
const uint8_t ADDR_GW_NODE[5] = {BACKBONE_RF_ADDR_GW_NODE_INIT};
// Gateway downlink pipe address — PC-originated commands arrive here (`GWSV1`-style bytes).
const uint8_t ADDR_GW_SERVER[5] = {BACKBONE_RF_ADDR_GW_SERVER_INIT};
// Concentrator address used when backbone transmits toward USB gateway (`SERV1`-style bytes).
const uint8_t ADDR_SERVER[5] = {BACKBONE_RF_ADDR_SERVER_INIT};

}  // namespace bbcfg
