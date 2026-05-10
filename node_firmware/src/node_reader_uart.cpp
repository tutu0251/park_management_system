// =============================================================================
// node_reader_uart.cpp — line-oriented USART parser + RESULT formatter
// =============================================================================
//
// LINE PARSING STATE MACHINE (COOPERATIVE / NON-BLOCKING)
// -------------------------------------------------------
// Arduino Serial RX ISR queues bytes; our poll function consumes available characters
// each loop iteration without busy-waiting entire lines — preserving responsiveness for
// radio servicing and watchdog kicking.
//
// TIMESTAMP / TIMEOUT NOTE
// ------------------------
// Individual UART bytes are NOT timed here — payment terminals normally emit full lines
// quickly. If byte-wise timeouts become necessary, extend this module with millis()
// stamps without blocking delay().
//
// WHY CRLF TERMINATORS
// --------------------
// Many UART peripherals treat LF as message end but CR stabilizes legacy parsers —
// emitting both avoids “half-printed” lines on some LCD UART adapters.
//
// =============================================================================

#include "node_reader_uart.h"

#include <Arduino.h>
#include <string.h>

#include "node_config.h"

static char s_line[NODE_RFID_LINE_MAX + 1];
static uint8_t s_len;
static bool s_overflow;

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
  return -1;
}

static bool parse_hex_payload(const char* hex, uint8_t* out, uint8_t* out_len) {
  const size_t hlen = strlen(hex);
  if (hlen == 0 || (hlen & 1U)) return false;
  if (hlen > 16U) return false;

  const uint8_t nbytes = static_cast<uint8_t>(hlen / 2U);
  if (nbytes > 8) return false;

  for (uint8_t i = 0; i < nbytes; ++i) {
    const int hi = hex_nibble(hex[i * 2U]);
    const int lo = hex_nibble(hex[i * 2U + 1U]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((static_cast<uint8_t>(hi) << 4) |
                                  static_cast<uint8_t>(lo));
  }
  *out_len = nbytes;
  return true;
}

static bool keyword_status(const char* kw, proto::MachineStatus* out) {
  if (!strcmp(kw, "MAINTENANCE") || !strcmp(kw, "maintenance")) {
    *out = proto::STATUS_MAINTENANCE;
    return true;
  }
  if (!strcmp(kw, "ONLINE") || !strcmp(kw, "online")) {
    *out = proto::STATUS_ONLINE;
    return true;
  }
  if (!strcmp(kw, "OFFLINE") || !strcmp(kw, "offline")) {
    *out = proto::STATUS_OFFLINE;
    return true;
  }
  if (!strcmp(kw, "ERROR") || !strcmp(kw, "error")) {
    *out = proto::STATUS_ERROR;
    return true;
  }
  return false;
}

static uint8_t dispatch_complete_line(uint8_t card_id[8], uint8_t* card_id_len,
                                      proto::MachineStatus* mode_out) {
  if (strncmp(s_line, "SWIPE,", 6) == 0) {
    const char* hex = s_line + 6;
    if (parse_hex_payload(hex, card_id, card_id_len)) return READER_EVT_SWIPE;
    node_reader_uart_send_result(proto::PAY_ERROR, 0);
    return READER_EVT_NONE;
  }

  if (strncmp(s_line, "MODE,", 5) == 0) {
    const char* kw = s_line + 5;
    if (keyword_status(kw, mode_out)) return READER_EVT_MODE;
    return READER_EVT_NONE;
  }

  if (strncmp(s_line, "STATUS,", 7) == 0) {
    const char* kw = s_line + 7;
    if (keyword_status(kw, mode_out)) return READER_EVT_MODE;
    return READER_EVT_NONE;
  }

  return READER_EVT_NONE;
}

static void serial_print_u32(uint32_t v) {
  if (v == 0) {
    Serial.write('0');
    return;
  }
  char tmp[10];
  uint8_t n = 0;
  while (v && n < sizeof(tmp)) {
    tmp[n++] = static_cast<char>('0' + (v % 10U));
    v /= 10U;
  }
  while (n) Serial.write(tmp[--n]);
}

void node_reader_uart_begin(void) {
  Serial.begin(NODE_RFID_UART_BAUD);
  s_len = 0;
  s_overflow = false;
}

uint8_t node_reader_uart_poll(uint8_t card_id[8], uint8_t* card_id_len,
                              proto::MachineStatus* mode_out) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;

    if (c == '\n') {
      if (s_overflow) {
        s_overflow = false;
        s_len = 0;
        continue;
      }
      s_line[s_len] = '\0';
      s_len = 0;
      return dispatch_complete_line(card_id, card_id_len, mode_out);
    }

    if (s_len < NODE_RFID_LINE_MAX) {
      s_line[s_len++] = c;
    } else {
      s_overflow = true;
    }
  }
  return READER_EVT_NONE;
}

void node_reader_uart_send_result(proto::PayResult result, uint32_t credit_remain) {
  Serial.print(F("RESULT,"));
  switch (result) {
    case proto::PAY_OK:
      Serial.print(F("Success"));
      break;
    case proto::PAY_FAIL:
      Serial.print(F("Fail"));
      break;
    case proto::PAY_ERROR:
    default:
      Serial.print(F("Error"));
      credit_remain = 0;
      break;
  }
  Serial.write(',');
  serial_print_u32(credit_remain);
  Serial.print(F("\r\n"));
}
