#pragma once
// =============================================================================
// backbone_router.h — packet routing + pending-swipe correlation for relay
// =============================================================================
//
// ROLE
// ----
// All radio I/O policy lives in backbone_radio.cpp; this module decides WHAT to
// send WHERE. It owns the small in-RAM state needed to correlate downlink
// PAY_RESP frames back to the reader that originated each SWIPE_REQ.
//
// CORRELATION MODEL
// -----------------
// PAY_RESP carries no transaction_id, so the backbone uses a fixed-depth FIFO
// ring of recent SWIPE_REQ frames. Each entry stores (node_id, deadline_ms).
// On each PAY_RESP we pop the oldest live entry and forward to that reader's
// "NODE{n}" address. Entries past their TTL are pruned every router tick so
// stale answers cannot mis-route to the wrong reader after packet loss.
//
// FAN-OUT FOR CHECK_STATUS
// ------------------------
// The gateway sends one CHECK_STATUS_REQ; the backbone broadcasts one copy to
// every entry in KNOWN_NODES so each reader replies with its own STATUS_RESP.
//
// SRAM BUDGET (ATmega8 — 1 KB total)
// ----------------------------------
// - KNOWN_NODES table: N * 7 bytes (node_id + 5-byte address); placed in flash.
// - Pending ring:      bbcfg::kPendingDepth * 6 bytes + 3 housekeeping bytes.
// =============================================================================

#include <stddef.h>
#include <stdint.h>

// One row in the static KNOWN_NODES table: logical id ↔ five-byte RX address.
struct NodeEntry {
  uint16_t node_id;
  uint8_t addr[5];
};

// Initialize router state (clears the pending ring). Safe to call twice.
void backbone_router_begin();

// Process a 32-byte frame received on the reader-side RX pipe (pipe 1).
// May allocate a pending-correlation slot for SWIPE_REQ uplinks and forwards
// the frame verbatim to the gateway. Returns true if the frame was forwarded.
bool backbone_router_handle_uplink(const uint8_t* frame32);

// Process a 32-byte frame received on the gateway-side RX pipe (pipe 2).
// Forwards PAY_RESP to the matching pending reader (FIFO) and fan-outs
// CHECK_STATUS_REQ to every KNOWN_NODES entry. Returns true on any forward.
bool backbone_router_handle_downlink(const uint8_t* frame32);

// Periodic housekeeping: prunes pending entries past their TTL. Pass millis().
void backbone_router_tick(uint32_t now_ms);

// Lookup helpers (also used by main.cpp for optional debug logging).
const NodeEntry* backbone_router_find_node(uint16_t node_id);
size_t backbone_router_known_count();
const NodeEntry* backbone_router_known_at(size_t i);

// Read-only stats counters; useful from main loop for periodic UART debug.
struct BackboneStats {
  uint32_t uplink_forwarded;
  uint32_t downlink_forwarded;
  uint32_t tx_failures;
  uint32_t pending_dropped;
  uint32_t orphan_pay_resp;
};
const BackboneStats& backbone_router_stats();
