#pragma once
// =============================================================================
// node_protocol.h — binary RF payload layouts shared with backbone_firmware
// =============================================================================
//
// BYTE ORDER / ALIGNMENT
// ----------------------
// AVR is little-endian for multi-byte integers — wire layout matches native memcpy
// of packed structs below. Do NOT insert implicit padding — __attribute__((packed))
// is mandatory on structs carried over-the-air.
//
// PAYLOAD WIDTH
// -------------
// proto::PAYLOAD_MAX is 32 — enforced by RF24::setPayloadSize and all writers.
// Logical messages shorter than 32 bytes MUST be zero-padded before TX so peers
// parsing fixed buffers never read uninitialized trailing bytes.
//
// MESSAGE MAP (type byte = buf[0])
// --------------------------------
// 0x01 SWIPE_REQ       Card tap uplink → backbone → billing server relay path.
// 0x10 CHECK_STATUS_REQ Backbone poll; node answers with STATUS_RESP (0x81).
// 0x11 STATUS_PUSH    Proactive status uplink (minimal 2-byte body + pad).
// 0x80 PAY_RESP       Billing outcome toward node (result + credit_after_cents).
// 0x81 STATUS_RESP    Reply to CHECK_STATUS_REQ with MachineStatus enum octet.
//
// DOCUMENTATION ALIASES (same numeric values as MsgType members)
// --------------------------------------------------------------
//   MSG_SWIPE         == SWIPE_REQ
//   MSG_PAY_RESULT    == PAY_RESP
//   MSG_CHECK_STATUS  == CHECK_STATUS_REQ
//   MSG_STATUS_REPORT == STATUS_PUSH (and STATUS_RESP answers polls)
//
// There is NO separate MSG_ERROR opcode on-air:
//   - Billing failure → PAY_RESP + PayResult::PAY_ERROR.
//   - RF/link escalation → STATUS_PUSH + MachineStatus::STATUS_ERROR (see main).
//
// STRUCT SIZES (verified by static_assert in node_protocol.cpp)
// ---------------------------------------------------------------
// SwipeReqPacked   — 20 bytes used inside the 32-byte window (rest zeros).
// PayRespPacked    — 6 bytes meaningful (type, result, uint32 credit).
// StatusRespPacked — 2 bytes (type + status enum).
//
// =============================================================================

#include <stddef.h>
#include <stdint.h>

namespace proto {

constexpr uint8_t PAYLOAD_MAX = 32;

enum MsgType : uint8_t {
  SWIPE_REQ = 0x01,
  CHECK_STATUS_REQ = 0x10,
  STATUS_PUSH = 0x11,
  PAY_RESP = 0x80,
  STATUS_RESP = 0x81,
};

constexpr uint8_t MSG_SWIPE = SWIPE_REQ;
constexpr uint8_t MSG_PAY_RESULT = PAY_RESP;
constexpr uint8_t MSG_CHECK_STATUS = CHECK_STATUS_REQ;
constexpr uint8_t MSG_STATUS_REPORT = STATUS_PUSH;

// Requirement label MSG_ERROR: no extra MsgType on-wire — billing errors use
// PAY_RESP + PayResult::PAY_ERROR; RF/link faults escalate to STATUS_ERROR pushes.

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

struct SwipeReqPacked {
  uint8_t type;
  uint16_t node_id;
  uint16_t reader_id;
  uint8_t card_uid_len;
  uint8_t card_uid[8];
  uint32_t transaction_id;
  uint16_t game_id;
} __attribute__((packed));

struct PayRespPacked {
  uint8_t type;
  uint8_t result;
  uint32_t credit_after_cents;
} __attribute__((packed));

struct StatusRespPacked {
  uint8_t type;
  uint8_t status;
} __attribute__((packed));

bool build_swipe(const uint8_t* card_uid, uint8_t uid_len, uint16_t node_id,
                 uint16_t reader_id, uint32_t transaction_id, uint16_t game_id,
                 uint8_t* out, size_t out_cap, size_t* out_len);

bool parse_rx(const uint8_t* buf, size_t len, PayResult* pay_out,
              uint32_t* credit_cents_out, bool* got_check_status);

size_t build_status_response(MachineStatus st, uint8_t* out, size_t out_cap);

size_t build_status_push(MachineStatus st, uint8_t* out, size_t out_cap);

const char* machine_status_name(MachineStatus st);
const char* pay_result_name(PayResult r);

}  // namespace proto
