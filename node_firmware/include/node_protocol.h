#pragma once
// =============================================================================
// node_protocol.h — compact BINARY layouts for nRF24 (reader node ↔ backbone)
// =============================================================================
//
// WHY THIS MODULE EXISTS
// ----------------------
// The reader MCU sits between two transports:
//   • UART to the RFID reader/payment terminal (human-facing device).
//   • SPI + nRF24 to the backbone (aggregates many machines and talks onward).
//
// UART can afford readable text because it is short, local, and easy to debug with
// a laptop. Over-the-air frames MUST stay small and deterministic: binary structs,
// fixed sizes, no snprintf on RF payloads. Smaller packets spend less time on air,
// which directly reduces collision probability when many nodes share one channel.
//
// SOURCE OF TRUTH / DEPLOYMENT WARNING
// ------------------------------------
// Any change to wire layouts must be mirrored in backbone + gateway + server code.
// Silent mismatches corrupt fields without crashing — always bump a protocol version
// if multiple firmware generations coexist in the field.
//
// HEADS-UP (2026 reader refresh): `SwipeReqPacked` now carries `type_id` + `event_type`,
// and `PayRespPacked` echoes `node_id`, `reader_id`, `card_id[]`, and `transaction_id` so
// billing replies cannot be applied to the wrong tap.  Older concentrator firmware MUST
// be upgraded alongside this image — partial upgrades look like “random declines”.
//
// ENDIANNESS AND PACKING (AVR / gcc)
// ----------------------------------
// Multi-byte integers are little-endian on AVR. Wire structs use __attribute__((packed))
// so the compiler cannot insert hidden padding that would shift byte offsets.
//
// WHY PAYLOAD_MAX == 32
// ---------------------
// nRF24L01+ Shockburst payloads are 1–32 bytes. Keeping every logical record ≤ 32 B:
//   • avoids fragmentation and variable-length complexity on this MCU class,
//   • matches RF24.setPayloadSize(32) everywhere,
//   • ensures TX/RX FIFO slots are uniform (simpler driver usage).
//
// ENUM ALIASES (SPEC NAMING)
// --------------------------
// MSG_* names mirror the product vocabulary; numeric values MUST remain stable.
//
// TRANSACTION MATCHING (WHY IT MATTERS)
// -------------------------------------
// Billing can be slower than RF retries. ACK on Shockburst only means the packet
// reached the peer radio — not that payment finished. Responses can arrive reordered
// or duplicated if higher layers retry. Therefore PAY_RESP carries transaction_id and
// card_id echo-back fields so THIS firmware accepts ONLY the response that belongs to
// the outstanding swipe (see main.cpp pending-transaction compare).
//
// =============================================================================

#include <stddef.h>
#include <stdint.h>

namespace proto {

/// Hardware/driver limit for Shockburst payloads on nRF24L01+.
constexpr uint8_t PAYLOAD_MAX = 32;

// ----- Message opcodes (buf[0]) ------------------------------------------------

enum MsgType : uint8_t {
  /// Payment request uplink after RFID terminal reports a swipe.
  SWIPE_REQ = 0x01,

  /// RF mesh diagnostic / application-level error tap (optional uplink/downlink).
  /// Distinct from PAY_RESP::PAY_ERROR (billing outcome vs transport/meta errors).
  MSG_ERROR = 0x0F,

  /// Backbone asks this node to report MachineStatus (poll / monitoring path).
  CHECK_STATUS_REQ = 0x10,

  /// Unsolicited heartbeat or immediate transition notification (Online/Maintenance/…).
  STATUS_PUSH = 0x11,

  /// Billing outcome relayed from backbone toward this reader node.
  PAY_RESP = 0x80,

  /// Answer to CHECK_STATUS_REQ — carries MachineStatus enum byte.
  STATUS_RESP = 0x81,
};

/// Alias names requested by product docs (same numeric values as primary constants).
constexpr uint8_t MSG_SWIPE = SWIPE_REQ;
constexpr uint8_t MSG_PAY_RESULT = PAY_RESP;
constexpr uint8_t MSG_CHECK_STATUS = CHECK_STATUS_REQ;
constexpr uint8_t MSG_STATUS_REPORT = STATUS_PUSH;

// ----- Billing result codes (binary RF + mirrored to UART RESULT lines) --------

enum PayResult : uint8_t {
  PAY_OK = 0,
  PAY_FAIL = 1,
  PAY_ERROR = 2,
};

// ----- Machine status (heartbeat + poll replies) --------------------------------

enum MachineStatus : uint8_t {
  STATUS_ONLINE = 0,
  STATUS_MAINTENANCE = 1,
  STATUS_OFFLINE = 2,
  STATUS_ERROR = 3,
};

// ----- Logical swipe discriminator (forward-compatible field in SWIPE_REQ) -----

enum SwipeEvent : uint8_t {
  EVT_NONE = 0,
  /// NFC/RFID tap presented for payment / play permission request.
  EVT_SWIPE = 1,
};

// =============================================================================
// SwipeReqPacked — RF uplink: “please bill / authorize this card at this machine”
// =============================================================================
//
// FIELD RATIONALE
// ---------------
// • type           — opcode dispatch on backbone/gateway parsers.
// • node_id        — WHICH physical reader station / lane (geo + cabinet identity).
// • reader_id      — WHICH payment terminal face when several readers share one MCU.
// • card_id_*      — compact credential reference (binary, NOT ASCII hex on-air).
// • transaction_id — unique id generated ON THIS NODE per swipe attempt; backbone
//                    MUST echo it in PAY_RESP so responses cannot be mismatched.
// • game_id        — WHICH priced experience / tariff bucket applies.
// • type_id        — product/category discriminator (games vs washers vs lockers…).
// • event_type     — today always EVT_SWIPE; reserved so future UART commands can
//                    reuse the same record shape without inventing new opcodes.
//
// MEMORY NOTE
// -----------
// Struct size stays small to preserve SRAM when staging copies on stack — still watch
// total stack depth across nested calls on ATmega8 (1 KiB SRAM total).
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

// =============================================================================
// PayRespPacked — RF downlink: billing outcome + echoed correlation fields
// =============================================================================
//
// WHY SEPARATE SUCCESS / FAIL / ERROR
// ---------------------------------
// • PAY_OK     — business approved; terminal should enable play if applicable.
// • PAY_FAIL   — honest decline (insufficient credits, blocked card, rule rejection).
// • PAY_ERROR  — transport/timeouts/gateway faults OR unknown codes hardened safe.
//
// WHY RETURN credit_remain
// ------------------------
// The payment terminal displays stored-value balances and decides UX tones/messages.
// This MCU only relays authoritative cents AFTER transaction evaluation.
//
struct PayRespPacked {
  uint8_t type;
  uint16_t node_id;
  uint16_t reader_id;
  uint8_t card_id_len;
  uint8_t card_id[8];
  uint32_t transaction_id;
  uint32_t credit_remain;
  /// Same numeric values as PayResult (Success/Fail/Error family).
  uint8_t event_type;
} __attribute__((packed));

// =============================================================================
// StatusRespPacked — short                                STATUS_RESP payload body
// =============================================================================

struct StatusRespPacked {
  uint8_t type;
  uint8_t status;
} __attribute__((packed));

// =============================================================================
// ErrorNotifyPacked — compact MSG_ERROR tap (optional; backbone defines meaning)
// =============================================================================

struct ErrorNotifyPacked {
  uint8_t type;
  uint8_t error_code;
} __attribute__((packed));

// ----- Builders / parsers -------------------------------------------------------

bool build_swipe(const uint8_t* card_id, uint8_t card_len, uint16_t node_id,
                 uint16_t reader_id, uint32_t transaction_id, uint16_t game_id,
                 uint8_t type_id, uint8_t swipe_event, uint8_t* out, size_t out_cap,
                 size_t* out_len);

bool parse_rx(const uint8_t* buf, size_t len, PayRespPacked* pay_out,
              bool* got_check_status, bool* got_error_notify);

size_t build_status_response(MachineStatus st, uint8_t* out, size_t out_cap);

size_t build_status_push(MachineStatus st, uint8_t* out, size_t out_cap);

size_t build_error_notify(uint8_t error_code, uint8_t* out, size_t out_cap);

const char* machine_status_name(MachineStatus st);
const char* pay_result_name(PayResult r);

}  // namespace proto
