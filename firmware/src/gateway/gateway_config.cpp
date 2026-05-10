// =============================================================================
// gateway_config.cpp — centralized storage for RF address bytes
// =============================================================================
//
// WHY THIS .cpp EXISTS (instead of constexpr arrays only in the header)
// -----------------------------------------------------------------------
// Multiple translation units include gateway_config.h; defining five-byte arrays in the
// header would violate C++ ODR unless marked inline (not portable across all AVR gcc flags).
// Keeping address blobs here guarantees exactly **one** linker-visible symbol per ADDR_*.
//
// =============================================================================

#include "gateway_config.h"

namespace gwcfg {

const uint8_t ADDR_GW_NODE[5] = {GATEWAY_RF_ADDR_GW_NODE_INIT};
const uint8_t ADDR_GW_SERVER[5] = {GATEWAY_RF_ADDR_GW_SERVER_INIT};
const uint8_t ADDR_SERVER[5] = {GATEWAY_RF_ADDR_SERVER_INIT};

}  // namespace gwcfg
