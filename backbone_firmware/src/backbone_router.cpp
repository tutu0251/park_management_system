// -----------------------------------------------------------------------------
// backbone_router.cpp — strict relay logic with FIFO pay-resp correlation
// -----------------------------------------------------------------------------
//
// FRAME FLOW (matches spec section 3 + 9)
// ---------------------------------------
//
//   pipe 1 RX (reader -> backbone):
//       SWIPE_REQ      -> push pending(node_id, deadline) -> TX to ADDR_SERVER
//       STATUS_PUSH    -> TX to ADDR_SERVER (no correlation needed)
//       STATUS_RESP    -> TX to ADDR_SERVER (no correlation needed)
//       <other>        -> drop (relay does not invent traffic)
//
//   pipe 2 RX (gateway -> backbone):
//       PAY_RESP       -> pop oldest pending -> TX to NODE{n} address
//       CHECK_STATUS_REQ -> fan-out one TX per entry in KNOWN_NODES
//       <other>        -> drop
//
// CORRELATION CAVEAT
// ------------------
// FIFO works because:
//   1) The on-air PAY_RESP frame deliberately omits transaction_id (legacy).
//   2) Reader nodes send only one swipe at a time (g_waiting_pay flag).
//   3) The gateway/PC answers swipes in the order it receives them.
// If a PAY_RESP is lost and a later one arrives, the TTL prune protects against
// mis-routing by dropping the orphaned head before it can claim the next answer.
//
// MEMORY DISCIPLINE
// -----------------
// - All buffers are static or stack-local 32-byte frames; no heap, no String.
// - The KNOWN_NODES array is `const PROGMEM` so flash holds the table and SRAM
//   only spends the read-into stack copy when forwarding.
//
// -----------------------------------------------------------------------------

#include "backbone_router.h"

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "backbone_config.h"
#include "backbone_protocol.h"
#include "backbone_radio.h"

static_assert(bbcfg::kPendingDepth > 0, "BACKBONE_PENDING_DEPTH must be > 0");
static_assert(bbcfg::kAppTxRetries > 0, "BACKBONE_APP_TX_RETRIES must be > 0");
static_assert(bbcfg::kTxBackoffMsMax > 0, "BACKBONE_TX_BACKOFF_MS_MAX must be > 0");

namespace {

// --- KNOWN_NODES table (flash-resident) -------------------------------------
//
// Edit this table to enroll more readers. Each row's addr[5] MUST equal the
// reader's RX pipe 1 address (node_cfg::node_listen_addr in node_firmware).
// Keeping the table in PROGMEM avoids burning ~28 bytes of SRAM per row.
const NodeEntry KNOWN_NODES[] PROGMEM = {
    {1, {'N', 'O', 'D', 'E', '1'}},
    {2, {'N', 'O', 'D', 'E', '2'}},
    {3, {'N', 'O', 'D', 'E', '3'}},
    {4, {'N', 'O', 'D', 'E', '4'}},
};
constexpr size_t kKnownNodeCount = sizeof(KNOWN_NODES) / sizeof(KNOWN_NODES[0]);

// Cached scratch row used when the caller asks for a NodeEntry pointer.
// Single-threaded loop() guarantees the caller consumes it before we overwrite.
NodeEntry g_scratch_entry;

// --- Pending swipe ring -----------------------------------------------------

struct PendingSwipe {
  uint16_t node_id;
  uint32_t deadline_ms;
};

PendingSwipe g_pending[bbcfg::kPendingDepth];
uint8_t g_head = 0;     // Next slot to pop (oldest live entry).
uint8_t g_tail = 0;     // Next slot to push (free slot).
uint8_t g_count = 0;    // Live entries in [head, tail).

// --- Diagnostics ------------------------------------------------------------

BackboneStats g_stats;
uint8_t g_tx_fail_streak = 0;

inline uint8_t ring_advance(uint8_t i) {
  // Branchless modulo for tiny power-of-two-ish depth; cheap on AVR.
  return static_cast<uint8_t>((i + 1U) % bbcfg::kPendingDepth);
}

// Returns true if `now_ms` is strictly past `deadline_ms` (wrap-safe via int32).
bool past_deadline(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void pending_drop_oldest() {
  if (g_count == 0) return;
  g_head = ring_advance(g_head);
  --g_count;
}

bool pending_push(uint16_t node_id, uint32_t now_ms) {
  if (node_id == 0) return false;

  // Ring full: drop the oldest unmatched swipe to make room. The original
  // reader will time out on its own (NODE_PAY_WAIT_MS in node_firmware).
  if (g_count == bbcfg::kPendingDepth) {
    ++g_stats.pending_dropped;
    pending_drop_oldest();
  }

  g_pending[g_tail].node_id = node_id;
  g_pending[g_tail].deadline_ms = now_ms + bbcfg::kPendingTtlMs;
  g_tail = ring_advance(g_tail);
  ++g_count;
  return true;
}

bool pending_pop_oldest(uint16_t* node_id_out) {
  if (g_count == 0) return false;
  if (node_id_out) *node_id_out = g_pending[g_head].node_id;
  g_head = ring_advance(g_head);
  --g_count;
  return true;
}

void pending_prune(uint32_t now_ms) {
  while (g_count > 0 && past_deadline(now_ms, g_pending[g_head].deadline_ms)) {
    ++g_stats.pending_dropped;
    pending_drop_oldest();
  }
}

// --- KNOWN_NODES helpers ----------------------------------------------------

void copy_node_entry_from_progmem(size_t idx, NodeEntry* out) {
  // memcpy_P pulls bytes from flash into a SRAM struct without burning a const
  // SRAM copy of the whole table. Caller owns `out`.
  memcpy_P(out, &KNOWN_NODES[idx], sizeof(NodeEntry));
}

// --- Fail counting helper ---------------------------------------------------

// Centralized accounting + automatic radio recovery on persistent failure.
void note_tx_result(bool ok) {
  if (ok) {
    g_tx_fail_streak = 0;
    return;
  }

  ++g_stats.tx_failures;
  if (++g_tx_fail_streak >= bbcfg::kRadioFailThreshold) {
    backbone_radio_recover();
    g_tx_fail_streak = 0;
  }
}

}  // namespace

void backbone_router_begin() {
  g_head = 0;
  g_tail = 0;
  g_count = 0;
  memset(&g_stats, 0, sizeof(g_stats));
  g_tx_fail_streak = 0;
}

bool backbone_router_handle_uplink(const uint8_t* frame32) {
  if (!frame32) return false;
  const uint8_t t = frame32[0];
  if (!proto::is_uplink_msg(t)) return false;   // Drop unknown / wrong-direction.

  // Track only SWIPE_REQ for FIFO correlation; status uplinks need none.
  if (t == proto::SWIPE_REQ) {
    uint16_t node_id = 0;
    if (proto::extract_swipe_node_id(frame32, &node_id)) {
      // We push BEFORE we send so a fast PAY_RESP cannot race ahead of us.
      pending_push(node_id, millis());
    }
    // If extract failed (malformed swipe) we still forward the bytes — the
    // gateway/PC layer is the authority on what is "valid".
  }

  // Forward verbatim toward the gateway; never modify payload bytes.
  const bool ok = backbone_radio_send_to(bbcfg::ADDR_SERVER, frame32);
  note_tx_result(ok);
  if (ok) ++g_stats.uplink_forwarded;
  return ok;
}

bool backbone_router_handle_downlink(const uint8_t* frame32) {
  if (!frame32) return false;
  const uint8_t t = frame32[0];
  if (!proto::is_downlink_msg(t)) return false;   // Drop unknown / wrong-direction.

  if (t == proto::PAY_RESP) {
    uint16_t node_id = 0;
    if (!pending_pop_oldest(&node_id)) {
      // No outstanding swipe — orphan PAY_RESP. Discard rather than guess.
      ++g_stats.orphan_pay_resp;
      return false;
    }

    uint8_t reader_addr[5];
    proto::build_node_address(node_id, reader_addr);

    const bool ok = backbone_radio_send_to(reader_addr, frame32);
    note_tx_result(ok);
    if (ok) ++g_stats.downlink_forwarded;
    return ok;
  }

  // CHECK_STATUS_REQ: fan-out one TX per known reader. Sequential is fine —
  // the table is tiny and each TX is bounded by RF24 retry timing.
  bool any_ok = false;
  NodeEntry tmp;
  for (size_t i = 0; i < kKnownNodeCount; ++i) {
    copy_node_entry_from_progmem(i, &tmp);
    const bool ok = backbone_radio_send_to(tmp.addr, frame32);
    note_tx_result(ok);
    if (ok) {
      ++g_stats.downlink_forwarded;
      any_ok = true;
    }
  }
  return any_ok;
}

void backbone_router_tick(uint32_t now_ms) {
  pending_prune(now_ms);
}

const NodeEntry* backbone_router_find_node(uint16_t node_id) {
  for (size_t i = 0; i < kKnownNodeCount; ++i) {
    NodeEntry tmp;
    copy_node_entry_from_progmem(i, &tmp);
    if (tmp.node_id == node_id) {
      g_scratch_entry = tmp;
      return &g_scratch_entry;
    }
  }
  return nullptr;
}

size_t backbone_router_known_count() {
  return kKnownNodeCount;
}

const NodeEntry* backbone_router_known_at(size_t i) {
  if (i >= kKnownNodeCount) return nullptr;
  copy_node_entry_from_progmem(i, &g_scratch_entry);
  return &g_scratch_entry;
}

const BackboneStats& backbone_router_stats() {
  return g_stats;
}
