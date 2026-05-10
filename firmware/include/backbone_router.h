#pragma once
// =============================================================================
// backbone_router.h — statistics + forwarding policy hooks for main.cpp
// =============================================================================

#include <stdint.h>

struct BackboneStats {
  uint32_t uplink_forwarded;
  uint32_t downlink_forwarded;
  uint32_t tx_failures;
  uint32_t pending_dropped;
  uint32_t orphan_pay_resp;
};

void backbone_router_begin(void);

bool backbone_router_handle_uplink(const uint8_t frame32[32]);

bool backbone_router_handle_downlink(const uint8_t frame32[32]);

void backbone_router_tick(uint32_t now_ms);

const BackboneStats& backbone_router_stats(void);
