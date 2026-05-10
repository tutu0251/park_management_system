// =============================================================================
// gateway_protocol.cpp — format RF frames for UART logs + pack PC commands for TX
// =============================================================================
//
// DESIGN GOAL
// -----------
// Operators can watch `Serial Monitor` and understand **money-path** traffic without a
// binary sniffer. Every `RX,…` line mirrors important fields from `park_rf_protocol.h`.
//
// STATIC ASSERTS
// --------------
// If someone edits packed structs in the shared header but forgets to update gateway,
// sizes change and compilation stops here — safer than subtle field shifts on-air.
//
// SWIPE PAYLOAD LAYOUT REMINDER
// -----------------------------
// `SwipeReqPacked` occupies the first 22 bytes. Bytes 22–31 are currently unused by the
// reader when packing SWIPE_REQ; we reuse that tail as an optional ASCII tag area for logs.
//
// =============================================================================

// Declares proto helpers and pulls shared packed layouts via park_rf_protocol.h.
#include "gateway_protocol.h"

// `Serial`, `F()` flash strings — UART logging for operators.
#include <Arduino.h>
// `memcpy`, `memset` for packing/unpacking fixed wire structs.
#include <string.h>

// Forward-used struct `GwPcCommand` for payment/check-status builders.
#include "gateway_serial.h"

// Compile-time guard: SWIPE_REQ layout must stay 22 bytes or telemetry decoders misalign.
static_assert(sizeof(proto::SwipeReqPacked) == 22, "SwipeReqPacked wire size");
// PAY_RESP fields must not drift — readers validate lengths strictly.
static_assert(sizeof(proto::PayRespPacked) == 23, "PayRespPacked wire size");
// STATUS_RESP is tiny — gateway prints human names from single status byte.
static_assert(sizeof(proto::StatusRespPacked) == 2, "StatusRespPacked wire size");
// Forwarding envelope toward readers — backbone inspects node_id first.
static_assert(sizeof(proto::CheckStatusFwdPacked) == 11, "CheckStatusFwdPacked wire size");

namespace proto {

// ----- UART helpers (decimal prints use Arduino Print; hex uses manual nibbles) --------

// Prints one hex digit (0–F) without pulling sprintf — saves flash on AVR.
static void print_hex_nibble(uint8_t v) {
  v &= 0x0F;
  Serial.print(static_cast<char>(v < 10 ? ('0' + v) : ('A' + (v - 10))));
}

// Expands binary UID bytes into uppercase hex text for CSV logs.
static void print_hex_uid(const uint8_t* uid, uint8_t len) {
  if (!uid || len == 0) return;
  if (len > 8) len = 8;
  for (uint8_t i = 0; i < len; ++i) {
    print_hex_nibble(uid[i] >> 4);
    print_hex_nibble(uid[i]);
  }
}

// Emits up to four printable ASCII bytes from RF tail or type_tag fields — '-' if empty/non-printable.
static void print_type_tag_ascii(const uint8_t* tag4) {
  if (!tag4) return;
  bool any = false;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t c = tag4[i];
    if (c == 0) break;
    if (c >= 32 && c <= 126) {
      Serial.print(static_cast<char>(c));
      any = true;
    }
  }
  if (!any) Serial.print('-');
}

bool swipe_wire_ok(const uint8_t* frame32) {
  if (!frame32) return false;
  // Byte 0 is message discriminator across all 32-byte park frames.
  if (frame32[0] != SWIPE_REQ) return false;
  SwipeReqPacked s{};
  // Interpret bytes 1.. as packed struct — AVR little-endian matches wire.
  memcpy(&s, frame32, sizeof(s));
  // Reject impossible lengths before dereferencing card_id in printers.
  if (s.card_id_len == 0 || s.card_id_len > sizeof(s.card_id)) return false;
  return true;
}

void serial_print_rx_swipe(const uint8_t* frame32) {
  SwipeReqPacked s{};
  memcpy(&s, frame32, sizeof(s));
  // Stable column order helps PC scripts grep the same keys as TX lines.
  Serial.print(F("RX,node_id="));
  Serial.print(s.node_id);
  Serial.print(F(",reader_id="));
  Serial.print(s.reader_id);
  Serial.print(F(",card_id="));
  print_hex_uid(s.card_id, s.card_id_len);
  Serial.print(F(",transaction_id="));
  Serial.print(s.transaction_id);
  Serial.print(F(",game_id="));
  Serial.print(s.game_id);
  Serial.print(F(",type_id="));
  Serial.print(s.type_id);
  Serial.print(F(",ascii="));
  // Bytes 22–31 documented as optional ASCII tag tail on SWIPE_REQ wire image.
  print_type_tag_ascii(&frame32[22]);
  Serial.print(F(",event_type=Swipe"));
  Serial.println();
}

void serial_print_rx_status(const uint8_t* frame32, uint8_t msg_type) {
  uint8_t st = 0;
  // PUSH variant carries raw status byte immediately after opcode.
  if (msg_type == STATUS_PUSH && PAYLOAD_MAX >= 2) {
    st = frame32[1];
  } else if (msg_type == STATUS_RESP && PAYLOAD_MAX >= sizeof(StatusRespPacked)) {
    StatusRespPacked p{};
    memcpy(&p, frame32, sizeof(p));
    st = p.status;
  } else {
    return;
  }

  MachineStatus ms = STATUS_ERROR;
  // Clamp unknown numeric codes to ERROR so enum dispatch stays safe.
  if (st <= STATUS_ERROR) ms = static_cast<MachineStatus>(st);

  Serial.print(F("RX,node_id=0,reader_id=0,game_id=0,type_id="));
  // STATUS frames stash a four-byte printable hint at offset 2 for logs.
  print_type_tag_ascii(&frame32[2]);
  Serial.print(F(",event_type="));
  Serial.print(machine_status_name(ms));
  Serial.println();
}

bool build_pay_frame(const GwPcCommand* cmd, uint8_t* out32) {
  if (!cmd || !out32) return false;

  PayRespPacked p{};
  p.type = PAY_RESP;
  p.node_id = cmd->node_id;
  p.reader_id = cmd->reader_id;
  p.card_id_len = cmd->card_id_len;
  if (p.card_id_len > sizeof(p.card_id)) return false;
  memcpy(p.card_id, cmd->card_id, p.card_id_len);
  p.transaction_id = cmd->transaction_id;
  // Zero credit when host omitted `credit_remain=` — still a valid PAY_RESP.
  p.credit_remain = cmd->has_credit ? cmd->credit_remain : 0UL;
  p.event_type = cmd->pay_result;

  // Full Shockburst width — trailing bytes must be zero for forward compatibility.
  memset(out32, 0, PAYLOAD_MAX);
  memcpy(out32, &p, sizeof(p));
  return true;
}

bool build_check_status_frame(const GwPcCommand* cmd, uint8_t* out32) {
  if (!cmd || !out32) return false;

  CheckStatusFwdPacked p{};
  p.type = CHECK_STATUS_REQ;
  p.node_id = cmd->node_id;
  p.reader_id = cmd->reader_id;
  p.game_id = cmd->game_id;
  memcpy(p.type_tag, cmd->type_tag, sizeof(p.type_tag));

  memset(out32, 0, PAYLOAD_MAX);
  memcpy(out32, &p, sizeof(p));
  return true;
}

}  // namespace proto
