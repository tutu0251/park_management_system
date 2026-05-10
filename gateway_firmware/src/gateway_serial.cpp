// -----------------------------------------------------------------------------
// gateway_serial.cpp — fixed line buffer, comma-separated key=value parser
// -----------------------------------------------------------------------------
#include "gateway_serial.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "gateway_config.h"
#include "gateway_protocol.h"

namespace {

#ifndef GW_SERIAL_LINE_MAX
#define GW_SERIAL_LINE_MAX 96
#endif

char g_line[GW_SERIAL_LINE_MAX];
uint8_t g_len;

static bool streq(const char* a, const char* b) {
  return strcmp(a, b) == 0;
}

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void trim_inplace(char* s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n > 0 && is_space(s[n - 1])) {
    s[--n] = '\0';
  }
  size_t i = 0;
  while (s[i] && is_space(s[i])) ++i;
  if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

static const char* split_key_value(char* pair, char** value_out) {
  *value_out = nullptr;
  char* eq = strchr(pair, '=');
  if (!eq) return nullptr;
  *eq = '\0';
  *value_out = eq + 1;
  return pair;
}

static bool parse_u16(const char* s, uint16_t* out) {
  if (!s || !*s || !out) return false;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 10);
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

static bool parse_event_type(const char* v, GwPcCommand* cmd) {
  if (!v || !cmd) return false;
  if (streq(v, "Success")) {
    cmd->kind = 1;
    cmd->pay_result = proto::PAY_OK;
    return true;
  }
  if (streq(v, "Fail")) {
    cmd->kind = 1;
    cmd->pay_result = proto::PAY_FAIL;
    return true;
  }
  if (streq(v, "Error")) {
    cmd->kind = 1;
    cmd->pay_result = proto::PAY_ERROR;
    return true;
  }
  if (streq(v, "CheckStatus")) {
    cmd->kind = 2;
    return true;
  }
  return false;
}

static bool parse_line_tokens(char* line, GwPcCommand* cmd) {
  memset(cmd, 0, sizeof(*cmd));
  cmd->kind = 0;

  trim_inplace(line);
  char* p = line;
  char* comma = strchr(p, ',');
  if (!comma) return false;
  *comma = '\0';
  trim_inplace(p);
  if (!streq(p, "TX")) return false;

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
      } else if (streq(key, "event_type")) {
        if (!parse_event_type(val, cmd)) return false;
      }
    }

    if (!comma) break;
    p = comma + 1;
  }

  return cmd->kind == 1 || cmd->kind == 2;
}

}  // namespace

void gw_serial_begin() {
  g_len = 0;
}

void gw_serial_print_banner() {
  Serial.print(F("GW"));
  Serial.print(static_cast<int>(gwcfg::kGatewayId));
  Serial.println(F("_ok"));
}

bool gw_serial_drain_command(GwPcCommand* out_cmd) {
  if (!out_cmd) return false;

  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
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

    if (g_len + 1 >= GW_SERIAL_LINE_MAX) {
      g_len = 0;
      continue;
    }
    g_line[g_len++] = static_cast<char>(c);
  }
  return false;
}

void gw_serial_print_err_tx_failed(const GwPcCommand* cmd) {
  Serial.print(F("ERR,node_id="));
  Serial.print(cmd ? cmd->node_id : 0);
  Serial.print(F(",reader_id="));
  Serial.print(cmd ? cmd->reader_id : 0);
  Serial.print(F(",transaction_id="));
  Serial.print(cmd ? cmd->transaction_id : 0UL);
  Serial.print(F(",reason=TX_FAILED"));
  Serial.println();
}
