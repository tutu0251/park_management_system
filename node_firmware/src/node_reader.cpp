// -----------------------------------------------------------------------------
// node_reader.cpp — RFID UID ingestion via SoftwareSerial (non-blocking)
// -----------------------------------------------------------------------------
//
// WHY SoftwareSerial HERE
// -----------------------
// ATmega8 exposes one hardware USART. That USART is dedicated to the game
// machine main board (see node_machine_uart.cpp — Arduino Serial).
//
// Many low-cost RFID UART modules expose async TX at 3V3/5V logic; we listen on a
// GPIO using SoftwareSerial (RX-only usage in practice: MCU RX ← module TX).
// The constructor still requires a dummy TX pin argument for the Arduino class —
// tie NODE_RFID_SOFT_TX_DUMMY_PIN to an unused pin (not connected to the module).
//
// LINE FORMAT (configurable length cap)
// -------------------------------------
// Modules commonly emit:
//   - An ASCII hex string (even digit count), terminated by '\n' (sometimes '\r\n').
// This parser accepts leading whitespace, rejects odd-length hex, caps output at
// 8 bytes (proto::SwipeReqPacked::card_uid max storage).
//
// OVERFLOW POLICY
// ---------------
// If the line exceeds NODE_RFID_LINE_MAX characters before newline, we reset the
// accumulator — prevents unbounded growth / stale UID merges on corrupt streams.
//
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>

#include "node_config.h"
#include "node_reader.h"

static SoftwareSerial g_rfid_serial(NODE_RFID_SOFT_RX_PIN, NODE_RFID_SOFT_TX_DUMMY_PIN);

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parse_hex_uid(const char* s, uint8_t* uid_out, uint8_t* uid_len_out) {
  if (!s || !uid_out || !uid_len_out) return false;
  while (*s == ' ' || *s == '\t') ++s;

  const size_t slen = strlen(s);
  if (slen < 2 || (slen % 2) != 0) return false;
  if (slen / 2 > 8) return false;

  for (size_t i = 0; i < slen; i += 2) {
    const int hi = hex_nibble(s[i]);
    const int lo = hex_nibble(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    uid_out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
  }
  *uid_len_out = static_cast<uint8_t>(slen / 2);
  return true;
}

void node_reader_begin() { g_rfid_serial.begin(NODE_RFID_UART_BAUD); }

bool node_reader_poll_swipe(uint8_t uid_out[8], uint8_t* uid_len_out) {
  static char line[NODE_RFID_LINE_MAX + 1];
  static uint8_t pos = 0;

  while (g_rfid_serial.available() > 0) {
    const char c = static_cast<char>(g_rfid_serial.read());
    if (c == '\r') continue;

    if (c == '\n') {
      line[pos] = '\0';
      pos = 0;
      if (line[0] != '\0' && parse_hex_uid(line, uid_out, uid_len_out)) return true;
      continue;
    }

    if (pos < NODE_RFID_LINE_MAX)
      line[pos++] = c;
    else
      pos = 0;
  }
  return false;
}
