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

// Declares the three gateway RF addresses and supplies the `GATEWAY_RF_ADDR_*_INIT` byte macros.
#include "gateway_config.h"

// Keeps gateway-specific linker symbols in one namespace; matches `gwcfg::` in headers and other .cpp files.
namespace gwcfg {

// On-air identity when other radios address “the gateway node” pipe (must match backbone/node tables).
const uint8_t ADDR_GW_NODE[5] = {GATEWAY_RF_ADDR_GW_NODE_INIT};
// Destination address for frames this gateway transmits toward the backbone concentrator path.
const uint8_t ADDR_GW_SERVER[5] = {GATEWAY_RF_ADDR_GW_SERVER_INIT};
// Reading pipe filter for uplink: backbone relays reader traffic into this five-byte pattern.
const uint8_t ADDR_SERVER[5] = {GATEWAY_RF_ADDR_SERVER_INIT};

}  // namespace gwcfg
