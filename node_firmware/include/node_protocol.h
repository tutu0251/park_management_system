#pragma once
// =============================================================================
// node_protocol.h — binary layout of nRF24 payloads (node ↔ gateway ↔ server)
// =============================================================================
// Contract: every on-air payload is exactly 32 bytes for RF24 fixed width;
// unused tail bytes are zero. Multi-byte integers are little-endian (AVR native).
// MUST stay byte-compatible with backbone_firmware/include/node_protocol.h.
// =============================================================================

#include <stddef.h>   // size_t in function prototypes
#include <stdint.h>   // uint8_t, uint16_t, uint32_t

namespace proto {

// RF24::setPayloadSize and buffer sizes for read/write paths.
constexpr uint8_t PAYLOAD_MAX = 32;

// First byte of every logical message discriminates parsing / routing.
enum MsgType : uint8_t {
  SWIPE_REQ = 0x01,         // Card tap uplink from node to server (via backbone).
  CHECK_STATUS_REQ = 0x10,  // Poll: request current machine status from node(s).
  STATUS_PUSH = 0x11,       // Unsolicited short status frame from node.
  PAY_RESP = 0x80,          // Authorization / balance result toward node.
  STATUS_RESP = 0x81,       // Reply to CHECK_STATUS_REQ with current status enum.
};

// Subset of business outcomes carried in PayRespPacked.result.
enum PayResult : uint8_t {
  PAY_OK = 0,      // Debit/credit path succeeded; credit_after_cents is valid.
  PAY_FAIL = 1,  // Declined (e.g. insufficient funds) — not a transport error.
  PAY_ERROR = 2, // System/timeout/parse failure — treat as safe "no play" path.
};

// Machine health / operator mode for STATUS_* messages.
enum MachineStatus : uint8_t {
  STATUS_ONLINE = 0,        // Normal operation; may suppress redundant pushes.
  STATUS_MAINTENANCE = 1, // Service mode — typically no customer play.
  STATUS_OFFLINE = 2,     // Intentionally unavailable.
  STATUS_ERROR = 3,       // Fault (e.g. radio streak) until cleared by operator.
};

// Packed wire format for SWIPE_REQ (total struct size asserted in .cpp).
// __attribute__((packed)) prevents compiler insertion of padding between fields.
struct SwipeReqPacked {
  uint8_t type;              // Always MsgType::SWIPE_REQ.
  uint16_t node_id;          // Which park machine (little-endian).
  uint16_t reader_id;        // Physical reader on that machine (multi-head setups).
  uint8_t card_uid_len;      // Count of valid bytes in card_uid (1..8 typical).
  uint8_t card_uid[8];       // Raw RFID UID bytes, left-aligned; rest ignored.
  uint32_t transaction_id;   // Monotonic id for idempotency / logging (LE).
  uint16_t game_id;          // Product or tariff selector for pricing rules.
} __attribute__((packed));

// Server → node result after processing a swipe (6 bytes used + padding to 32).
struct PayRespPacked {
  uint8_t type;                   // Always MsgType::PAY_RESP.
  uint8_t result;                // PayResult numeric code.
  uint32_t credit_after_cents;   // Post-transaction stored value in cents (LE).
} __attribute__((packed));

// Compact status answer (may be sent as full 32-byte frame with zero tail).
struct StatusRespPacked {
  uint8_t type;    // MsgType::STATUS_RESP.
  uint8_t status;  // MachineStatus numeric code.
} __attribute__((packed));

// Serialize a card swipe request into out; sets *out_len to bytes written.
bool build_swipe(const uint8_t* card_uid, uint8_t uid_len, uint16_t node_id,
                 uint16_t reader_id, uint32_t transaction_id, uint16_t game_id,
                 uint8_t* out, size_t out_cap, size_t* out_len);

// Decode PAY_RESP or detect CHECK_STATUS_REQ in a received buffer.
// Optional out-pointers may be null if caller only needs part of the result.
bool parse_rx(const uint8_t* buf, size_t len, PayResult* pay_out,
              uint32_t* credit_cents_out, bool* got_check_status);

// Build STATUS_RESP (two-byte packed struct) into out; returns 0 on cap error.
size_t build_status_response(MachineStatus st, uint8_t* out, size_t out_cap);

// Build minimal two-byte STATUS_PUSH [type][status] for proactive uplink.
size_t build_status_push(MachineStatus st, uint8_t* out, size_t out_cap);

// Human-readable labels for Serial logging (Flash string not used — ROM const).
const char* machine_status_name(MachineStatus st);
const char* pay_result_name(PayResult r);

}  // namespace proto
