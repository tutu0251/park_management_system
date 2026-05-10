// -----------------------------------------------------------------------------
// node_protocol.cpp — pack/unpack helpers for fixed-width RF payloads (node)
// -----------------------------------------------------------------------------
#include "node_protocol.h"  // Declarations and packed struct layouts

#include <string.h>           // memcpy, memset

// Compile-time guard: if struct layout drifts, wire format breaks silently.
static_assert(sizeof(proto::SwipeReqPacked) == 20, "unexpected SwipeReqPacked size");
static_assert(sizeof(proto::PayRespPacked) == 6, "unexpected PayRespPacked size");
static_assert(sizeof(proto::StatusRespPacked) == 2, "unexpected StatusRespPacked size");

namespace proto {

bool build_swipe(const uint8_t* card_uid, uint8_t uid_len, uint16_t node_id,
                 uint16_t reader_id, uint32_t transaction_id, uint16_t game_id,
                 uint8_t* out, size_t out_cap, size_t* out_len) {
  // Reject null inputs, empty UID, UID longer than field, or undersized buffer.
  if (!card_uid || uid_len == 0 || uid_len > sizeof(SwipeReqPacked::card_uid) ||
      !out || out_cap < sizeof(SwipeReqPacked) || !out_len) {
    return false;
  }

  SwipeReqPacked p{};              // Value-init: zero including padding gaps.
  p.type = SWIPE_REQ;              // Discriminator for parsers on backbone/server.
  p.node_id = node_id;             // Copied as native uint16_t (LE on wire).
  p.reader_id = reader_id;
  p.card_uid_len = uid_len;        // Allows variable-length UID without sentinel.
  memcpy(p.card_uid, card_uid, uid_len);  // Remaining card_uid bytes stay zero.
  p.transaction_id = transaction_id;
  p.game_id = game_id;

  memcpy(out, &p, sizeof(p));      // Bitwise copy to caller buffer (may be unaligned-safe here).
  *out_len = sizeof(p);            // Caller pads/truncates to PAYLOAD_MAX if needed.
  return true;
}

bool parse_rx(const uint8_t* buf, size_t len, PayResult* pay_out,
              uint32_t* credit_cents_out, bool* got_check_status) {
  if (!buf || len == 0) return false;  // No payload to interpret.

  // Defensive defaults so partial failure leaves deterministic outputs.
  if (pay_out) *pay_out = PAY_ERROR;
  if (credit_cents_out) *credit_cents_out = 0;
  if (got_check_status) *got_check_status = false;

  switch (buf[0]) {                // Branch on message type byte at offset 0.
    case PAY_RESP: {
      if (len < sizeof(PayRespPacked)) return false;  // Truncated frame.
      PayRespPacked p;
      memcpy(&p, buf, sizeof(p));   // Deserialize from possibly unaligned buf.
      if (pay_out) {
        // Clamp unknown result codes to PAY_ERROR for forward compatibility.
        if (p.result <= PAY_ERROR)
          *pay_out = static_cast<PayResult>(p.result);
        else
          *pay_out = PAY_ERROR;
      }
      if (credit_cents_out) *credit_cents_out = p.credit_after_cents;
      return true;
    }
    case CHECK_STATUS_REQ:
      if (got_check_status) *got_check_status = true;  // One-byte request is valid.
      return true;
    default:
      return false;                // Unknown type — caller should ignore frame.
  }
}

size_t build_status_response(MachineStatus st, uint8_t* out, size_t out_cap) {
  if (!out || out_cap < sizeof(StatusRespPacked)) return 0;  // Cannot fit struct.

  StatusRespPacked p{};
  p.type = STATUS_RESP;
  p.status = static_cast<uint8_t>(st);  // Narrow enum to wire octet.
  memcpy(out, &p, sizeof(p));
  return sizeof(p);                // Caller may still send full 32-byte buffer.
}

size_t build_status_push(MachineStatus st, uint8_t* out, size_t out_cap) {
  if (!out || out_cap < 2) return 0;  // Minimal STATUS_PUSH is exactly two bytes.

  out[0] = STATUS_PUSH;            // Type without separate packed struct.
  out[1] = static_cast<uint8_t>(st);
  return 2;
}

const char* machine_status_name(MachineStatus st) {
  switch (st) {
    case STATUS_ONLINE:
      return "Online";
    case STATUS_MAINTENANCE:
      return "Maintenance";
    case STATUS_OFFLINE:
      return "Offline";
    case STATUS_ERROR:
      return "Error";
    default:
      return "?";                  // Unknown enum value from corrupt wire data.
  }
}

const char* pay_result_name(PayResult r) {
  switch (r) {
    case PAY_OK:
      return "Success";
    case PAY_FAIL:
      return "Fail";
    case PAY_ERROR:
      return "Error";
    default:
      return "?";
  }
}

}  // namespace proto
