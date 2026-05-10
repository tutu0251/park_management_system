#pragma once
// =============================================================================
// node_protocol.h — binary layout of nRF24 payloads (shared with node_firmware)
// =============================================================================
// Contract: every on-air payload is exactly 32 bytes; tail zero-padded.
// Multi-byte integers are little-endian (ATmega328 native). Any change must be
// mirrored in node_firmware/include/node_protocol.h and server-side code.
// =============================================================================

#include <stddef.h>
#include <stdint.h>

namespace proto {

constexpr uint8_t PAYLOAD_MAX = 32;

enum MsgType : uint8_t {
  SWIPE_REQ = 0x01,         // Card tap from node; backbone forwards to server.
  CHECK_STATUS_REQ = 0x10,  // Poll command from backbone or server relay path.
  STATUS_PUSH = 0x11,       // Proactive two-byte status from node.
  PAY_RESP = 0x80,          // Billing outcome from server to backbone to node.
  STATUS_RESP = 0x81,       // Answer to CHECK_STATUS_REQ with MachineStatus.
};

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
