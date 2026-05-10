// -----------------------------------------------------------------------------
// gateway_protocol.cpp — tiny serializers + RX line formatting (no heap)
// -----------------------------------------------------------------------------
#include "gateway_protocol.h"

#include <Arduino.h>
#include <string.h>

static_assert(sizeof(proto::SwipeReqPacked) == 20, "SwipeReqPacked wire size");
static_assert(sizeof(proto::PayRespPacked) == 6, "PayRespPacked wire size");
static_assert(sizeof(proto::StatusRespPacked) == 2, "StatusRespPacked wire size");

namespace proto {

static void print_hex_nibble(uint8_t v) {
  v &= 0x0F;
  Serial.print(static_cast<char>(v < 10 ? ('0' + v) : ('A' + (v - 10))));
}

void print_hex_uid(const uint8_t* uid, uint8_t len) {
  if (!uid || len == 0) return;
  if (len > 8) len = 8;
  for (uint8_t i = 0; i < len; ++i) {
    print_hex_nibble(uid[i] >> 4);
    print_hex_nibble(uid[i]);
  }
}

void print_type_tag_ascii(const uint8_t* tag4) {
  if (!tag4) return;
  bool any = false;
  for (uint8_t i = 0; i < SWIPE_TYPE_TAG_LEN; ++i) {
    const uint8_t c = tag4[i];
    if (c == 0) break;
    if (c >= 32 && c <= 126) {
      Serial.print(static_cast<char>(c));
      any = true;
    }
  }
  if (!any) Serial.print('-');
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
      return "?";
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

bool swipe_wire_ok(const uint8_t* frame32) {
  if (!frame32) return false;
  if (frame32[0] != SWIPE_REQ) return false;
  SwipeReqPacked s;
  memcpy(&s, frame32, sizeof(s));
  if (s.card_uid_len == 0 || s.card_uid_len > sizeof(s.card_uid)) return false;
  return true;
}

void serial_print_rx_swipe(const uint8_t* frame32) {
  SwipeReqPacked s;
  memcpy(&s, frame32, sizeof(s));
  Serial.print(F("RX,node_id="));
  Serial.print(s.node_id);
  Serial.print(F(",reader_id="));
  Serial.print(s.reader_id);
  Serial.print(F(",card_id="));
  print_hex_uid(s.card_uid, s.card_uid_len);
  Serial.print(F(",transaction_id="));
  Serial.print(s.transaction_id);
  Serial.print(F(",game_id="));
  Serial.print(s.game_id);
  Serial.print(F(",type_id="));
  print_type_tag_ascii(&frame32[SWIPE_TYPE_TAG_OFFSET]);
  Serial.print(F(",event_type=Swipe"));
  Serial.println();
}

void serial_print_rx_status(const uint8_t* frame32, uint8_t msg_type) {
  uint8_t st = 0;
  if (msg_type == STATUS_PUSH && proto::PAYLOAD_MAX >= 2) {
    st = frame32[1];
  } else if (msg_type == STATUS_RESP && proto::PAYLOAD_MAX >= sizeof(StatusRespPacked)) {
    StatusRespPacked p;
    memcpy(&p, frame32, sizeof(p));
    st = p.status;
  } else {
    return;
  }

  MachineStatus ms = STATUS_ERROR;
  if (st <= STATUS_ERROR) ms = static_cast<MachineStatus>(st);

  // Wire format does not carry node/game/type for short status frames; keep
  // placeholders so the PC parser stays line-stable. Optional tail bytes 2..5
  // could be used later for reader_id LE without breaking current nodes.
  Serial.print(F("RX,node_id=0,reader_id=0,game_id=0,type_id="));
  print_type_tag_ascii(&frame32[2]);
  Serial.print(F(",event_type="));
  Serial.print(machine_status_name(ms));
  Serial.println();
}

bool build_pay_frame(uint8_t result_code, uint32_t credit_cents, uint8_t* out32) {
  if (!out32) return false;
  memset(out32, 0, PAYLOAD_MAX);
  PayRespPacked p{};
  p.type = PAY_RESP;
  p.result = result_code;
  p.credit_after_cents = credit_cents;
  memcpy(out32, &p, sizeof(p));
  return true;
}

bool build_check_status_frame(uint8_t* out32) {
  if (!out32) return false;
  memset(out32, 0, PAYLOAD_MAX);
  out32[0] = CHECK_STATUS_REQ;
  return true;
}

}  // namespace proto
