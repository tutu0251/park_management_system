#pragma once
// =============================================================================
// backbone_protocol.h — wire layout shared with node_firmware + gateway_firmware
// =============================================================================
//
// PHILOSOPHY
// ----------
// The backbone is a strict relay: it forwards 32-byte frames byte-for-byte and
// only inspects the leading type byte (and node_id inside SWIPE_REQ for routing
// metadata bookkeeping). It never modifies a payload before retransmission.
//
// PAYLOAD WIDTH
// -------------
// Every on-air ShockBurst payload is fixed at 32 bytes (PAYLOAD_MAX). Logical
// messages shorter than that MUST be zero-padded by the producer; the relay
// preserves trailing bytes verbatim so future fields stay forward-compatible.
//
// MESSAGE MAP (== buf[0])
// -----------------------
//   0x01 SWIPE_REQ        Reader → backbone → gateway (uplink).
//   0x10 CHECK_STATUS_REQ Gateway → backbone → readers (downlink fan-out).
//   0x11 STATUS_PUSH      Reader → backbone → gateway (uplink, periodic).
//   0x80 PAY_RESP         Gateway → backbone → reader (downlink, FIFO routed).
//   0x81 STATUS_RESP      Reader → backbone → gateway (uplink, reply to poll).
//
// LOGICAL ALIASES (requested in spec)
// -----------------------------------
//   MSG_SWIPE         == SWIPE_REQ
//   MSG_PAY_RESULT    == PAY_RESP
//   MSG_CHECK_STATUS  == CHECK_STATUS_REQ
//   MSG_STATUS_REPORT == STATUS_PUSH (and STATUS_RESP answers polls)
//   MSG_ERROR         == NO dedicated opcode on-air. Billing failure travels in
//                       PAY_RESP with PayResult::PAY_ERROR; link/relay faults
//                       are surfaced as STATUS_PUSH+MachineStatus::STATUS_ERROR
//                       by reader nodes (the backbone never originates one).
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

// Spec-required logical aliases. Same numeric values as MsgType members above.
constexpr uint8_t MSG_SWIPE = SWIPE_REQ;
constexpr uint8_t MSG_PAY_RESULT = PAY_RESP;
constexpr uint8_t MSG_CHECK_STATUS = CHECK_STATUS_REQ;
constexpr uint8_t MSG_STATUS_REPORT = STATUS_PUSH;
// MSG_ERROR is intentionally absent from on-air encoding; see header notes.

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

// 20 bytes (the rest of the 32-byte frame is zero-padded by the reader).
struct SwipeReqPacked {
  uint8_t type;
  uint16_t node_id;
  uint16_t reader_id;
  uint8_t card_uid_len;
  uint8_t card_uid[8];
  uint32_t transaction_id;
  uint16_t game_id;
} __attribute__((packed));

// 6 bytes; PAY_RESP carries no node_id, so the backbone uses FIFO correlation.
struct PayRespPacked {
  uint8_t type;
  uint8_t result;
  uint32_t credit_after_cents;
} __attribute__((packed));

// 2 bytes; tail of the 32-byte frame is reserved zero-pad.
struct StatusRespPacked {
  uint8_t type;
  uint8_t status;
} __attribute__((packed));

// --- Routing helpers --------------------------------------------------------

// Reader-originated message types the backbone forwards toward the gateway.
bool is_uplink_msg(uint8_t t);

// Gateway-originated message types the backbone forwards toward reader nodes.
bool is_downlink_msg(uint8_t t);

// Pulls node_id out of a SWIPE_REQ frame; returns false if frame[0] != SWIPE_REQ
// or node_id field is zero (unaddressable). Output is undefined on failure.
bool extract_swipe_node_id(const uint8_t* frame32, uint16_t* node_id_out);

// Produce reader's nRF24 listening address from a node_id (mirrors
// node_cfg::node_listen_addr in node_firmware): "NODE" + ('0' + node_id).
// Caller supplies a 5-byte output buffer; safe for node_id 1..9.
void build_node_address(uint16_t node_id, uint8_t out_addr[5]);

// Debug helpers (only meaningful when BACKBONE_DEBUG_UART != 0 in caller).
const char* msg_type_name(uint8_t t);

}  // namespace proto
