// =============================================================================
// backbone_router.cpp — opaque 32-byte relay with minimal bookkeeping
// =============================================================================
//
// Forwarding rules:
//   • Pipe #1 (reader → backbone): always forwarded verbatim toward ADDR_SERVER so the
//     USB gateway can deserialize billing events for the PC.
//   • Pipe #2 (gateway → backbone): PAY_RESP / CHECK_STATUS_REQ carry a destination
//     node_id inside the packed struct; those frames are relayed toward “NODE{n}”.
//
// PAY_RESP correlation is carried in-band (see PayRespPacked); the historical SRAM-heavy
// FIFO of pending swipes is therefore unnecessary for routing — only statistics remain.
//
// METRICS FIELD NOTES (BackboneStats)
// -----------------------------------
//   uplink_forwarded   — reader→gateway frames handed to ADDR_SERVER TX successfully.
//   downlink_forwarded — gateway→reader frames handed to NODE{n} TX successfully.
//   tx_failures        — increments whenever either direction cannot deliver after retries.
//   pending_dropped    — reserved counter (legacy pending-queue era); currently unused.
//   orphan_pay_resp    — misnamed historical label: increments on **any** downlink frame we
//                        could not route (unknown opcode, illegal node_id, oversized structs).
//
// =============================================================================

#include "backbone_router.h"

#include <string.h>

#include "backbone_config.h"
#include "backbone_radio.h"
#include "park_rf_protocol.h"

namespace {

BackboneStats g_stats{};
uint8_t g_tx_fail_streak = 0;

static void on_tx_fail(void) {
  ++g_stats.tx_failures;
  if (++g_tx_fail_streak >= bbcfg::kRadioFailThreshold) {
    backbone_radio_recover();
    g_tx_fail_streak = 0;
  }
}

static void on_tx_ok(void) { g_tx_fail_streak = 0; }

}  // namespace

void backbone_router_begin(void) {
  memset(&g_stats, 0, sizeof(g_stats));
  g_tx_fail_streak = 0;
}

bool backbone_router_handle_uplink(const uint8_t frame32[32]) {
  if (!frame32) return false;
  if (backbone_radio_send_to_server(frame32)) {
    ++g_stats.uplink_forwarded;
    on_tx_ok();
    return true;
  }
  on_tx_fail();
  return false;
}

bool backbone_router_handle_downlink(const uint8_t frame32[32]) {
  if (!frame32) return false;

  uint16_t node = 0;

  switch (frame32[0]) {
    case proto::PAY_RESP: {
      if (sizeof(proto::PayRespPacked) > proto::PAYLOAD_MAX) return false;
      proto::PayRespPacked pay{};
      memcpy(&pay, frame32, sizeof(pay));
      node = pay.node_id;
      break;
    }
    case proto::CHECK_STATUS_REQ: {
      if (sizeof(proto::CheckStatusFwdPacked) > proto::PAYLOAD_MAX) return false;
      proto::CheckStatusFwdPacked chk{};
      memcpy(&chk, frame32, sizeof(chk));
      node = chk.node_id;
      break;
    }
    default:
      // Unexpected opcode from gateway pipe — do not guess destinations; surface via stats.
      ++g_stats.orphan_pay_resp;
      return false;
  }

  if (node == 0 || node > 9) {
    // Current NODE{n} ASCII scheme only supports single-digit suffixes ('1'..'9').
    ++g_stats.orphan_pay_resp;
    return false;
  }

  if (backbone_radio_send_to_reader(node, frame32)) {
    ++g_stats.downlink_forwarded;
    on_tx_ok();
    return true;
  }

  on_tx_fail();
  return false;
}

void backbone_router_tick(uint32_t /*now_ms*/) {
  // Reserved for future TTL-based housekeeping (legacy pending-swipe queues). Today all
  // routing fields travel inside the 32-byte frames themselves.
  (void)BACKBONE_PENDING_TTL_MS;
  (void)bbcfg::kPendingDepth;
}

const BackboneStats& backbone_router_stats(void) { return g_stats; }
