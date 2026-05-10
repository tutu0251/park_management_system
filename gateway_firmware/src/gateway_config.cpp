// -----------------------------------------------------------------------------
// gateway_config.cpp — storage for const nRF24 address bytes
// -----------------------------------------------------------------------------
#include "gateway_config.h"

namespace gwcfg {

const uint8_t ADDR_GW_NODE[5] = {GATEWAY_RF_ADDR_GW_NODE_INIT};
const uint8_t ADDR_GW_SERVER[5] = {GATEWAY_RF_ADDR_GW_SERVER_INIT};
const uint8_t ADDR_SERVER[5] = {GATEWAY_RF_ADDR_SERVER_INIT};

}  // namespace gwcfg
