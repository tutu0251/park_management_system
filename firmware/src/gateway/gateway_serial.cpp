// =============================================================================
// gateway_serial.cpp — parse PC → gateway serial commands (“TX,…” lines)
// =============================================================================
//
// WHY THIS FILE EXISTS
// --------------------
// The ATmega8 gateway sits between a PC (human-readable CSV-ish UART lines) and the
// binary nRF24 world. This module is deliberately dumb: it does **not** decide whether a
// payment should succeed — it only turns text into `GwPcCommand` structs so
// gateway_protocol.cpp can build exact `proto::*Packed` bytes that match reader nodes.
//
// LINE FORMAT (what the PC sends)
// -------------------------------
//   TX,<key=value>,<key=value>,...
//
// Keys we understand today:
//   • node_id, reader_id — logical addressing echoed inside RF structs.
//   • transaction_id — MUST match the swipe the reader is waiting on (see below).
//   • card_id — hex string (even length, ≤16 hex chars → ≤8 bytes), MUST match reader’s pending UID.
//   • credit_remain — optional cents/units for PAY_OK / PAY_FAIL displays on the terminal.
//   • game_id, type_id — carried on CheckStatus commands as routing/metadata context.
//   • event_type — selects command family:
//       - Success | Fail | Error  → payment response (kind == 1)
//       - CheckStatus             → poll reader for STATUS_RESP (kind == 2)
//
// WHY card_id + transaction_id ARE MANDATORY FOR PAYMENTS
// --------------------------------------------------------
// Reader firmware keeps a **single** `PendingSwipe` after Shockburst-ACK of SWIPE_REQ.
// PAY_RESP is accepted only if **transaction_id**, **card bytes**, and **node/reader ids**
// all match that pending tuple. Without card_id in the PC command the gateway cannot
// forge a legal PAY_RESP — the reader would (correctly) ignore it as stale/spoofed.
//
// MEMORY / SAFETY RULES (ATmega8 has ~1 KiB SRAM)
// ------------------------------------------------
//   • One static line buffer `g_line` — no heap, no Arduino `String`.
//   • If a line grows past GW_SERIAL_LINE_MAX we **discard** the partial line on '\n'
//     rather than overflowing RAM (host can retry).
//   • `parse_line_tokens` mutates the buffer (inserts '\0') — parse copy-once per line.
//
// NON-BLOCKING BEHAVIOR
// ---------------------
// `gw_serial_drain_command` reads whatever bytes are available from `Serial` **once per call**
// (typically from `loop`). It never spins waiting for a full line — keeps gateway responsive
// for RF RX and watchdog pets in main.cpp.
//
// =============================================================================

// Declares GwPcCommand + public UART parser API implemented in this file.
#include "gateway_serial.h"

// Serial.read/available/print — interaction with USB-UART bridge on gateway MCU.
#include <Arduino.h>
// `strtoul` for bounded decimal parsing without sscanf flash cost.
#include <stdlib.h>
// `strcmp`, `strchr`, `strlen`, `memmove`, `memset` — line parsing without heap.
#include <string.h>

// Only needed here for `gw_serial_print_banner` reading numeric gateway id constant.
#include "gateway_config.h"

namespace {

// Maximum PC line length including terminating NUL we append before parsing.
// Keep modest: longer lines cost RAM forever (static storage duration).
#ifndef GW_SERIAL_LINE_MAX
#define GW_SERIAL_LINE_MAX 120
#endif

// Assembled line under construction between '\n' delimiters.
char g_line[GW_SERIAL_LINE_MAX];

// Number of characters currently stored in g_line (excluding terminator until finalize).
uint8_t g_len;

// ----- Tiny string utilities -------------------------------------------------

// Lexicographic compare helper keeps token checks readable vs strcmp boilerplate.
static bool streq(const char* a, const char* b) { return strcmp(a, b) == 0; }

// Treat common whitespace as ignorable around comma-separated tokens.
static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Trim leading/trailing ASCII whitespace in-place so " TX " still parses as TX after split.
static void trim_inplace(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && is_space(s[n - 1])) {
    s[--n] = '\0';
  }
  size_t i = 0;
  while (s[i] && is_space(s[i])) ++i;
  // Compact trimmed prefix toward buffer start — preserves terminating NUL.
  if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

// Split "key=value" into key pointer + value pointer; inserts '\0' over '=' (**mutates** input).
static const char* split_key_value(char* pair, char** value_out) {
  *value_out = nullptr;
  char* eq = strchr(pair, '=');
  if (!eq) return nullptr;
  *eq = '\0';
  *value_out = eq + 1;
  return pair;
}

// ----- Numeric parsers (decimal unsigned only) -------------------------------

static bool parse_u16(const char* s, uint16_t* out) {
  if (!s || !*s || !out) return false;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 10);
  // Reject empty scan or overflow past 16-bit destination.
  if (end == s || v > 65535UL) return false;
  *out = static_cast<uint16_t>(v);
  return true;
}

static bool parse_u32(const char* s, uint32_t* out) {
  if (!s || !*s || !out) return false;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 10);
  if (end == s) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

// ----- Hex UID parser (matches node_reader_uart expectations) -----------------

// Returns 0–15 for valid hex digit or -1 for illegal characters.
static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
  return -1;
}

// Converts even-length hex (max 16 nibbles → 8 bytes) into binary credential bytes.
static bool parse_hex_uid(const char* hex, uint8_t* out, uint8_t* out_len) {
  const size_t hlen = strlen(hex);
  if (hlen == 0 || (hlen & 1U)) return false;
  if (hlen > 16U) return false;

  const uint8_t nbytes = static_cast<uint8_t>(hlen / 2U);
  if (nbytes > 8) return false;

  for (uint8_t i = 0; i < nbytes; ++i) {
    const int hi = hex_nibble(hex[i * 2U]);
    const int lo = hex_nibble(hex[i * 2U + 1U]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((static_cast<uint8_t>(hi) << 4) | static_cast<uint8_t>(lo));
  }
  *out_len = nbytes;
  return true;
}

// Copy up to four printable ASCII chars from PC into RF type_tag bytes (zero padded).
// Stops early on comma/newline so trailing junk on the UART line cannot overrun.
static void copy_type_tag(const char* v, uint8_t tag[4]) {
  memset(tag, 0, 4);
  if (!v) return;
  for (uint8_t i = 0; i < 4 && v[i]; ++i) {
    const char c = v[i];
    if (c == ',' || c == '\r' || c == '\n') break;
    tag[i] = static_cast<uint8_t>(c);
  }
}

// Maps textual event_type token into GwPcCommand.kind + pay_result for payments.
// kind == 1 : PC wants us to emit PAY_RESP bytes toward the reader path.
// kind == 2 : PC wants us to emit CHECK_STATUS_REQ bytes toward the reader path.
static bool parse_event_type(const char* v, GwPcCommand* cmd) {
  if (!v || !cmd) return false;
  if (streq(v, "Success")) {
    cmd->kind = 1;
    cmd->pay_result = 0;  // mirrors proto::PAY_OK
    return true;
  }
  if (streq(v, "Fail")) {
    cmd->kind = 1;
    cmd->pay_result = 1;  // mirrors proto::PAY_FAIL
    return true;
  }
  if (streq(v, "Error")) {
    cmd->kind = 1;
    cmd->pay_result = 2;  // mirrors proto::PAY_ERROR
    return true;
  }
  if (streq(v, "CheckStatus")) {
    cmd->kind = 2;
    return true;
  }
  return false;
}

// Last-pass validation after all key=value pairs were applied.
static bool finalize_command(GwPcCommand* cmd) {
  if (cmd->kind == 1) {
    // Payment replies must correlate with the outstanding swipe on the reader MCU.
    if (cmd->card_id_len == 0) return false;
    if (cmd->transaction_id == 0) return false;
    return true;
  }
  if (cmd->kind == 2) {
    // Backbone routing uses node_id from CheckStatusFwdPacked — zero is illegal here.
    if (cmd->node_id == 0) return false;
    return true;
  }
  return false;
}

// Tokenizer: splits commas, then each token as key=value. Unknown keys are ignored so PC
// software can append forward-compatible fields without breaking older gateways.
static bool parse_line_tokens(char* line, GwPcCommand* cmd) {
  memset(cmd, 0, sizeof(*cmd));
  cmd->kind = 0;

  trim_inplace(line);
  char* p = line;
  char* comma = strchr(p, ',');
  if (!comma) return false;
  *comma = '\0';
  trim_inplace(p);
  // First CSV cell must be literal verb `TX` — distinguishes future protocol versions.
  if (!streq(p, "TX")) return false;

  // Everything after first comma is zero or more `key=value` pairs.
  p = comma + 1;
  while (*p) {
    comma = strchr(p, ',');
    if (comma) *comma = '\0';

    trim_inplace(p);
    char* val = nullptr;
    const char* key = split_key_value(p, &val);
    if (key && val) {
      if (streq(key, "node_id")) {
        (void)parse_u16(val, &cmd->node_id);
      } else if (streq(key, "reader_id")) {
        (void)parse_u16(val, &cmd->reader_id);
      } else if (streq(key, "transaction_id")) {
        (void)parse_u32(val, &cmd->transaction_id);
      } else if (streq(key, "credit_remain")) {
        if (parse_u32(val, &cmd->credit_remain)) cmd->has_credit = true;
      } else if (streq(key, "card_id")) {
        (void)parse_hex_uid(val, cmd->card_id, &cmd->card_id_len);
      } else if (streq(key, "game_id")) {
        (void)parse_u16(val, &cmd->game_id);
      } else if (streq(key, "type_id")) {
        copy_type_tag(val, cmd->type_tag);
      } else if (streq(key, "event_type")) {
        // Unknown payment verbs abort the line — prevents silent misroutes.
        if (!parse_event_type(val, cmd)) return false;
      }
    }

    if (!comma) break;
    p = comma + 1;
  }

  return finalize_command(cmd);
}

}  // namespace

// Reset UART reassembly — safe to call multiple times (e.g., after watchdog).
void gw_serial_begin(void) { g_len = 0; }

void gw_serial_print_banner(void) {
  Serial.print(F("GW"));
  Serial.print(static_cast<int>(gwcfg::kGatewayId));
  Serial.println(F("_ok"));
}

bool gw_serial_drain_command(GwPcCommand* out_cmd) {
  if (!out_cmd) return false;

  // Drain at most what arrived since last call — cooperative multitasking with RF loop.
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;

    // Ignore CR so Windows terminals sending CRLF still behave like LF-only parsers.
    if (c == '\r') continue;

    if (c == '\n') {
      // Line overflow policy: drop silently — prevents RAM exhaustion on garbage streams.
      if (g_len >= GW_SERIAL_LINE_MAX) {
        g_len = 0;
        continue;
      }
      g_line[g_len] = '\0';
      g_len = 0;
      if (g_line[0] == '\0') continue;

      GwPcCommand tmp{};
      if (!parse_line_tokens(g_line, &tmp)) continue;
      *out_cmd = tmp;
      return true;
    }

    // Mid-line overflow — discard partial command; host must resend entire line.
    if (g_len + 1 >= GW_SERIAL_LINE_MAX) {
      g_len = 0;
      continue;
    }
    g_line[g_len++] = static_cast<char>(c);
  }
  return false;
}

void gw_serial_print_err_tx_failed(const GwPcCommand* cmd) {
  // Mirrors ACK columns so log parsers can join success/failure rows by IDs.
  Serial.print(F("ERR,node_id="));
  Serial.print(cmd ? cmd->node_id : 0);
  Serial.print(F(",reader_id="));
  Serial.print(cmd ? cmd->reader_id : 0);
  Serial.print(F(",transaction_id="));
  Serial.print(cmd ? cmd->transaction_id : 0UL);
  Serial.print(F(",reason=TX_FAILED"));
  Serial.println();
}
