// Include guard — all three MCU roles must compile identical struct layouts from this file.
#pragma once
// =============================================================================
// park_rf_protocol.h — SINGLE SOURCE OF TRUTH for on-air binary frames (32 B max)
// =============================================================================
//
// SYSTEM CONTEXT (three MCU roles share this header verbatim)
// -------------------------------------------------------------
//   Reader NODE (RFID UART)  <--nRF24-->  BACKBONE (relay)  <--nRF24-->  GATEWAY (USB serial)  <-->  PC
//
// Every SWIPE_REQ, PAY_RESP, STATUS_*, and MSG_ERROR layout change MUST be compiled into
// all three images together. Silent drift corrupts money-path fields without crashing.
//
// PAYLOAD LIMIT
// -------------
// nRF24L01+ Shockburst payloads are 1–32 bytes. All logical records are packed to ≤32 B.
//
// ENDIANNESS
// ----------
// AVR is little-endian; multi-byte integers are naturally laid out LSB-first in memory.
//
// CHECK_STATUS ROUTING
// --------------------
// Nodes only inspect byte[0] for CHECK_STATUS_REQ. The backbone still needs a destination,
// so the gateway fills CheckStatusFwdPacked (node_id, reader_id, …) after the opcode.
// Reader firmware ignores those trailing bytes; backbone reads them for RF addressing only.
//
// =============================================================================

// size_t for buffer capacity parameters on builders.
#include <stddef.h>
// Exact-width integers for packed structs — endianness documented in banner above.
#include <stdint.h>

namespace proto {

// Nordic Shockburst upper bound enforced everywhere RF24 is used.
constexpr uint8_t PAYLOAD_MAX = 32;

enum MsgType : uint8_t {
  SWIPE_REQ = 0x01,        ///< Credential / authorization request from reader toward PC path.
  MSG_ERROR = 0x0F,        ///< Mesh diagnostic — distinct from PAY_RESP::PAY_ERROR decline path.
  CHECK_STATUS_REQ = 0x10, ///< Backbone/gateway poll — reader answers with STATUS_RESP.
  STATUS_PUSH = 0x11,      ///< Unsolicited heartbeat / transition broadcast from reader.
  PAY_RESP = 0x80,         ///< Billing outcome + correlation fields toward reader UART RESULT.
  STATUS_RESP = 0x81,      ///< Reply carrying MachineStatus snapshot after poll.
};

constexpr uint8_t MSG_SWIPE = SWIPE_REQ;
constexpr uint8_t MSG_PAY_RESULT = PAY_RESP;
constexpr uint8_t MSG_CHECK_STATUS = CHECK_STATUS_REQ;
constexpr uint8_t MSG_STATUS_REPORT = STATUS_PUSH;

enum PayResult : uint8_t {
  PAY_OK = 0,
  PAY_FAIL = 1,
  PAY_ERROR = 2,
};

enum MachineStatus : uint8_t {
  STATUS_ONLINE = 0,
  STATUS_MAINTENANCE = 1,
  STATUS_OFFLINE = 2,
  STATUS_ERROR = 3,
};

enum SwipeEvent : uint8_t {
  EVT_NONE = 0,
  EVT_SWIPE = 1,
};

// -----------------------------------------------------------------------------
// SwipeReqPacked — reader → backbone → gateway (payment / authorization request)
// -----------------------------------------------------------------------------
// Semantics:
//   Issued when the payment terminal reports a credential tap. This frame rides reader→
//   backbone RF pipe #1, backbone forwards verbatim toward ADDR_SERVER where gateway hears it.
//
// Field guide:
//   type        — MUST be SWIPE_REQ (see MsgType). Backbone/gateway dispatch on byte[0].
//   node_id     — Physical cabinet / lane identity within the park mesh.
//   reader_id   — Face/device index when multiple terminals share one MCU.
//   card_id_len — Byte length of credential snapshot (≤8). Zero is illegal on trusted paths.
//   card_id[]   — Raw credential bytes (NOT ASCII hex — UART hex translation happens earlier).
//   transaction_id — Monotonic per-node swipe handle; echoed in PAY_RESP for correlation.
//   game_id     — Tariff / attraction bucket selected by deployment constants or future UART.
//   type_id     — Compact billing/category discriminator (distinct from game_id).
//   event_type  — Today always EVT_SWIPE; reserved so future UART verbs reuse struct shape.
//
struct SwipeReqPacked {
  uint8_t type;
  uint16_t node_id;
  uint16_t reader_id;
  uint8_t card_id_len;
  uint8_t card_id[8];
  uint32_t transaction_id;
  uint16_t game_id;
  uint8_t type_id;
  uint8_t event_type;
} __attribute__((packed));

// -----------------------------------------------------------------------------
// PayRespPacked — PC → gateway → backbone → reader (billing outcome + correlation)
// -----------------------------------------------------------------------------
// Semantics:
//   Returned after server evaluation. Reader firmware ONLY accepts PAY_RESP matching its
//   pending swipe tuple — prevents mis-payment if duplicates/reordering occur on RF retries.
//
// Field guide:
//   type          — MUST be PAY_RESP.
//   node_id/reader_id — Copied from authoritative billing record (must match reader compile-time IDs).
//   card_id_len/card_id — Echo credential bytes from SWIPE_REQ so hostile stray frames fail closed.
//   transaction_id — MUST match pending swipe generation counter on reader MCU.
//   credit_remain — Post-transaction stored value shown on terminal UART RESULT line.
//   event_type    — Encodes PayResult (Success/Fail/Error). Unknown codes clamp to PAY_ERROR.
//
struct PayRespPacked {
  uint8_t type;
  uint16_t node_id;
  uint16_t reader_id;
  uint8_t card_id_len;
  uint8_t card_id[8];
  uint32_t transaction_id;
  uint32_t credit_remain;
  uint8_t event_type;
} __attribute__((packed));

// -----------------------------------------------------------------------------
// StatusRespPacked — short STATUS_RESP body
// -----------------------------------------------------------------------------
// Sent when reader answers backbone CHECK_STATUS_REQ polls while staying ONLINE/etc.
struct StatusRespPacked {
  uint8_t type;    ///< STATUS_RESP opcode for parsers.
  uint8_t status;  ///< MachineStatus enum numeric value (Online/Maintenance/Offline/Error).
} __attribute__((packed));

// -----------------------------------------------------------------------------
// ErrorNotifyPacked — optional mesh diagnostic tap
// -----------------------------------------------------------------------------
// Distinct from PAY_RESP::PAY_ERROR — represents transport/meta faults decided upstream.
struct ErrorNotifyPacked {
  uint8_t type;        ///< MSG_ERROR opcode.
  uint8_t error_code;  ///< Domain-specific code owned by gateway/backbone policy tables.
} __attribute__((packed));

// -----------------------------------------------------------------------------
// CheckStatusFwdPacked — gateway → backbone routing envelope (node ignores tail)
// -----------------------------------------------------------------------------
// Reader firmware treats **any** packet whose byte[0]==CHECK_STATUS_REQ as a poll request.
// Extra routing bytes exist purely so backbone_router.cpp can openWritingPipe("NODE{n}")
// without maintaining historical swipe FIFOs.
//
struct CheckStatusFwdPacked {
  uint8_t type;          ///< CHECK_STATUS_REQ opcode.
  uint16_t node_id;      ///< Destination cabinet radio — MUST match NODE{n} addressing scheme.
  uint16_t reader_id;    ///< Echoed for observability / future multi-face filtering.
  uint16_t game_id;      ///< Optional tariff context from PC commissioning tools.
  uint8_t type_tag[4];   ///< Printable-ish tag (e.g., “T003”) copied from UART command lines.
} __attribute__((packed));

// ----- Builders / parsers (node + backbone + gateway) ------------------------
//
// build_swipe — Validates lengths/capacity, fills SwipeReqPacked, returns logical wire length.
//
// parse_rx — Opcode dispatch for reader RX path:
//              PAY_RESP → fills PayRespPacked + clamps unknown results.
//              CHECK_STATUS_REQ → sets got_check_status (payload tail ignored by reader).
//              MSG_ERROR → sets got_error_notify after minimum length check.
//
// build_status_response / build_status_push — STATUS_RESP vs compact heartbeat STATUS_PUSH.
//
// build_error_notify — Constructs MSG_ERROR diagnostic tap (optional feature across mesh).
//
// machine_status_name / pay_result_name / msg_type_name — UART/debug pretty printers.

bool build_swipe(const uint8_t* card_id, uint8_t card_len, uint16_t node_id, uint16_t reader_id,
                 uint32_t transaction_id, uint16_t game_id, uint8_t type_id, uint8_t swipe_event,
                 uint8_t* out, size_t out_cap, size_t* out_len);

bool parse_rx(const uint8_t* buf, size_t len, PayRespPacked* pay_out, bool* got_check_status,
              bool* got_error_notify);

size_t build_status_response(MachineStatus st, uint8_t* out, size_t out_cap);

size_t build_status_push(MachineStatus st, uint8_t* out, size_t out_cap);

size_t build_error_notify(uint8_t error_code, uint8_t* out, size_t out_cap);

const char* machine_status_name(MachineStatus st);
const char* pay_result_name(PayResult r);
const char* msg_type_name(uint8_t t);

}  // namespace proto
