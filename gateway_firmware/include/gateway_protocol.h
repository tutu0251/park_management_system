#pragma once
// =============================================================================
// gateway_protocol.h — on-air wire layout (must match node_protocol.h)
// =============================================================================
// Logical names requested for the gateway map to the shared repo MsgType
// values so backbone and reader nodes stay compatible without forked RF stacks.
//
// MSG_SWIPE            -> SWIPE_REQ (0x01)
// MSG_PAY_RESULT       -> PAY_RESP (0x80)
// MSG_CHECK_STATUS     -> CHECK_STATUS_REQ (0x10)
// MSG_STATUS_REPORT    -> STATUS_PUSH (0x11) / STATUS_RESP (0x81)
// MSG_ERROR            -> PAY_RESP result=PAY_ERROR or MachineStatus ERROR
// =============================================================================

#include <stddef.h>
#include <stdint.h>

namespace proto {

constexpr uint8_t PAYLOAD_MAX = 32;

// Wire message types (identical to backbone_firmware/include/node_protocol.h).
enum MsgType : uint8_t {
  SWIPE_REQ = 0x01,
  CHECK_STATUS_REQ = 0x10,
  STATUS_PUSH = 0x11,
  PAY_RESP = 0x80,
  STATUS_RESP = 0x81,
};

// Aliases for documentation / gateway naming.
constexpr MsgType MSG_SWIPE = SWIPE_REQ;
constexpr MsgType MSG_PAY_RESULT = PAY_RESP;
constexpr MsgType MSG_CHECK_STATUS = CHECK_STATUS_REQ;
constexpr MsgType MSG_STATUS_REPORT_PUSH = STATUS_PUSH;
constexpr MsgType MSG_STATUS_REPORT_RESP = STATUS_RESP;

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

// Bytes 20..23 inside a 32-byte swipe frame: optional ASCII type tag (T003).
// Reader/backbone firmware may leave zero; serial prints empty or "----".
constexpr uint8_t SWIPE_TYPE_TAG_OFFSET = 20;
constexpr uint8_t SWIPE_TYPE_TAG_LEN = 4;

const char* machine_status_name(MachineStatus st);
const char* pay_result_name(PayResult r);

void print_hex_uid(const uint8_t* uid, uint8_t len);
void print_type_tag_ascii(const uint8_t* tag4);

bool swipe_wire_ok(const uint8_t* frame32);
void serial_print_rx_swipe(const uint8_t* frame32);
void serial_print_rx_status(const uint8_t* frame32, uint8_t msg_type);

bool build_pay_frame(uint8_t result_code, uint32_t credit_cents, uint8_t* out32);
bool build_check_status_frame(uint8_t* out32);

}  // namespace proto
