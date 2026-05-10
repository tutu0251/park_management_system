// Include guard.
#pragma once
// =============================================================================
// backbone_router.h — statistics + forwarding policy hooks for main.cpp
// =============================================================================

#include <stdint.h>

// Counters support optional debug UART heartbeat — fields documented in backbone_router.cpp banner.
struct BackboneStats {
  uint32_t uplink_forwarded;    // Reader→gateway frames delivered via ADDR_SERVER TX.
  uint32_t downlink_forwarded;  // Gateway→reader frames delivered via NODE{n} TX.
  uint32_t tx_failures;         // Either direction exhausted retries.
  uint32_t pending_dropped;     // Legacy placeholder — currently unused.
  uint32_t orphan_pay_resp;     // Misrouted / unroutable downlink frames (see router comments).
};

// Zero statistics — call on boot and after successful radio revive.
void backbone_router_begin(void);

// Forward verbatim uplink frame toward gateway concentrator.
bool backbone_router_handle_uplink(const uint8_t frame32[32]);

// Inspect opcode + embedded node_id, then TX toward matching NODE{n}.
bool backbone_router_handle_downlink(const uint8_t frame32[32]);

// Hook for future TTL-based queues — today references constants only.
void backbone_router_tick(uint32_t now_ms);

// Read-only metrics snapshot for debug prints.
const BackboneStats& backbone_router_stats(void);
