// -----------------------------------------------------------------------------
// backbone_protocol.cpp — small, no-heap helpers used by the relay router
// -----------------------------------------------------------------------------
//
// SCOPE
// -----
// The backbone is a strict relay; it never builds payment frames or status
// frames itself. This TU therefore exposes only the minimum needed to:
//   1) classify a 32-byte frame as uplink or downlink, and
//   2) extract node_id from a SWIPE_REQ for routing bookkeeping, and
//   3) regenerate a reader's RF address from its node_id.
//
// AVR LAYOUT NOTES
// ----------------
// We rely on packed struct layout matching the wire bytes. static_asserts below
// catch silent layout drift before flashing the device.
//
// -----------------------------------------------------------------------------

#include "backbone_protocol.h"

#include <string.h>

static_assert(sizeof(proto::SwipeReqPacked) == 20, "SwipeReqPacked wire size");
static_assert(sizeof(proto::PayRespPacked) == 6, "PayRespPacked wire size");
static_assert(sizeof(proto::StatusRespPacked) == 2, "StatusRespPacked wire size");

namespace proto {

bool is_uplink_msg(uint8_t t) {
  return t == SWIPE_REQ || t == STATUS_PUSH || t == STATUS_RESP;
}

bool is_downlink_msg(uint8_t t) {
  return t == PAY_RESP || t == CHECK_STATUS_REQ;
}

bool extract_swipe_node_id(const uint8_t* frame32, uint16_t* node_id_out) {
  if (!frame32 || !node_id_out) return false;
  if (frame32[0] != SWIPE_REQ) return false;

  // memcpy avoids any unaligned access trap (AVR is byte-addressable but the
  // packed struct path is the safe, portable way to honor the wire layout).
  SwipeReqPacked s;
  memcpy(&s, frame32, sizeof(s));

  if (s.node_id == 0) return false;   // node_id 0 is reserved / undeliverable.
  *node_id_out = s.node_id;
  return true;
}

void build_node_address(uint16_t node_id, uint8_t out_addr[5]) {
  // ASCII bytes "NODE" + ('0' + node_id_lsb). Mirrors node_cfg::node_listen_addr
  // exactly so reader nodes match this address on their RX pipe 1.
  out_addr[0] = 'N';
  out_addr[1] = 'O';
  out_addr[2] = 'D';
  out_addr[3] = 'E';
  // Caller is responsible for keeping node_id in 1..9 range (single ASCII digit).
  out_addr[4] = static_cast<uint8_t>('0' + (node_id & 0x0F));
}

const char* msg_type_name(uint8_t t) {
  switch (t) {
    case SWIPE_REQ:
      return "SWIPE";
    case CHECK_STATUS_REQ:
      return "CHK";
    case STATUS_PUSH:
      return "PUSH";
    case STATUS_RESP:
      return "STAT";
    case PAY_RESP:
      return "PAY";
    default:
      return "?";
  }
}

}  // namespace proto
