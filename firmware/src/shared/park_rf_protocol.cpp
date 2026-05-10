// =============================================================================
// park_rf_protocol.cpp — pack/unpack + debug names for all park RF roles
// =============================================================================
//
// WHY KEEP THIS FILE ARDUINO-FREE
// -------------------------------
// Pulling <Arduino.h> here would drag Print formatting dependencies into every firmware
// image even though only gateway UART helpers need them. Shared serialization stays lean:
// only `<string.h>` for memcpy/memset so unit tests / simulators can reuse the same TU.
//
// FAILURE PHILOSOPHY
// ------------------
// Builders return false / zero length instead of partial writes — callers must abort RF TX
// rather than emitting truncated structs that look valid on-air but decode into garbage.
//
// =============================================================================

#include "park_rf_protocol.h"

#include <string.h>

static_assert(sizeof(proto::SwipeReqPacked) <= proto::PAYLOAD_MAX, "SwipeReqPacked");
static_assert(sizeof(proto::PayRespPacked) <= proto::PAYLOAD_MAX, "PayRespPacked");
static_assert(sizeof(proto::StatusRespPacked) <= proto::PAYLOAD_MAX, "StatusRespPacked");
static_assert(sizeof(proto::ErrorNotifyPacked) <= proto::PAYLOAD_MAX, "ErrorNotifyPacked");
static_assert(sizeof(proto::CheckStatusFwdPacked) <= proto::PAYLOAD_MAX, "CheckStatusFwdPacked");

namespace proto {

// Assembles SWIPE_REQ bytes into caller-provided buffer; pads not required — caller pads to 32 B for RF24.
bool build_swipe(const uint8_t* card_id, uint8_t card_len, uint16_t node_id, uint16_t reader_id,
                 uint32_t transaction_id, uint16_t game_id, uint8_t type_id, uint8_t swipe_event,
                 uint8_t* out, size_t out_cap, size_t* out_len) {
  if (!card_id || card_len == 0 || card_len > sizeof(SwipeReqPacked::card_id) || !out ||
      out_cap < sizeof(SwipeReqPacked) || !out_len) {
    return false;
  }

  SwipeReqPacked p{};
  p.type = SWIPE_REQ;
  p.node_id = node_id;
  p.reader_id = reader_id;
  p.card_id_len = card_len;
  memcpy(p.card_id, card_id, card_len);
  p.transaction_id = transaction_id;
  p.game_id = game_id;
  p.type_id = type_id;
  p.event_type = swipe_event;

  memcpy(out, &p, sizeof(p));
  *out_len = sizeof(p);
  return true;
}

// Reader RX multiplexer: separates polling checks, payment replies, and mesh error taps.
bool parse_rx(const uint8_t* buf, size_t len, PayRespPacked* pay_out, bool* got_check_status,
              bool* got_error_notify) {
  if (!buf || len == 0) return false;

  if (pay_out) memset(pay_out, 0, sizeof(*pay_out));
  if (got_check_status) *got_check_status = false;
  if (got_error_notify) *got_error_notify = false;

  switch (buf[0]) {
    case PAY_RESP: {
      if (len < sizeof(PayRespPacked)) return false;
      if (pay_out) {
        memcpy(pay_out, buf, sizeof(PayRespPacked));
        if (pay_out->event_type > PAY_ERROR) pay_out->event_type = PAY_ERROR;
      }
      return true;
    }
    case CHECK_STATUS_REQ:
      if (got_check_status) *got_check_status = true;
      return true;

    case MSG_ERROR: {
      if (len < sizeof(ErrorNotifyPacked)) return false;
      if (got_error_notify) *got_error_notify = true;
      return true;
    }

    default:
      return false;
  }
}

// Backbone CHECK_STATUS poll answer — carries MachineStatus enum inside STATUS_RESP opcode.
size_t build_status_response(MachineStatus st, uint8_t* out, size_t out_cap) {
  if (!out || out_cap < sizeof(StatusRespPacked)) return 0;

  StatusRespPacked p{};
  p.type = STATUS_RESP;
  p.status = static_cast<uint8_t>(st);
  memcpy(out, &p, sizeof(p));
  return sizeof(p);
}

// Compact heartbeat — two bytes only so periodic telemetry stays collision-friendly on busy channels.
size_t build_status_push(MachineStatus st, uint8_t* out, size_t out_cap) {
  if (!out || out_cap < 2) return 0;

  out[0] = STATUS_PUSH;
  out[1] = static_cast<uint8_t>(st);
  return 2;
}

// Optional MSG_ERROR generator — upstream logic assigns semantic meaning to error_code bytes.
size_t build_error_notify(uint8_t error_code, uint8_t* out, size_t out_cap) {
  if (!out || out_cap < sizeof(ErrorNotifyPacked)) return 0;
  ErrorNotifyPacked p{};
  p.type = MSG_ERROR;
  p.error_code = error_code;
  memcpy(out, &p, sizeof(p));
  return sizeof(p);
}

// Stable textual tokens for UART debug bridges (never localize — host parsers depend on English tokens).
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
      return "?";
  }
}

// Mirrors billing vocabulary emitted on payment-terminal UART RESULT lines.
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

// Backbone debug UART helper — prints opcode keyword for quick eyeballing during bring-up.
const char* msg_type_name(uint8_t t) {
  switch (t) {
    case SWIPE_REQ:
      return "SWIPE_REQ";
    case MSG_ERROR:
      return "MSG_ERROR";
    case CHECK_STATUS_REQ:
      return "CHECK_STATUS_REQ";
    case STATUS_PUSH:
      return "STATUS_PUSH";
    case PAY_RESP:
      return "PAY_RESP";
    case STATUS_RESP:
      return "STATUS_RESP";
    default:
      return "OTHER";
  }
}

}  // namespace proto
