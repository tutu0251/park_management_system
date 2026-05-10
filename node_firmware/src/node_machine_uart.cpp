// -----------------------------------------------------------------------------
// node_machine_uart.cpp — ASCII line protocol to/from the game machine board
// -----------------------------------------------------------------------------
//
// PHYSICAL LAYER
// --------------
// Uses Arduino Serial — on ATmega8 MiniCore this maps to USART0 (pins PD0/PD1).
// Baud configured by NODE_MACHINE_UART_BAUD in node_config.h (default 9600).
//
// DESIGN SPLIT WITH main.cpp
// --------------------------
// This TU only parses inbound commands into proto::MachineStatus updates (pointer
// write). main.cpp detects transitions and then:
//   - Sends matching STATUS_* lines back (notify),
//   - Issues STATUS_PUSH binary frames toward backbone,
//   - Clears RF failure streak when returning to ONLINE (operator recovery path).
//
// OUTBOUND LINES (node → main board)
// ----------------------------------
// Billing outcomes:
//   GAME_START    — PAY_OK / sufficient credit path granted play.
//   PAY_FAIL      — Declined payment path (e.g. insufficient funds).
//   SYSTEM_ERROR  — Transport / timeout / malformed flow — conservative safe deny.
//
// Machine visibility:
//   STATUS_ONLINE / STATUS_OFFLINE / STATUS_MAINTENANCE / STATUS_ERROR
//
// All outbound helpers use F() macros so format strings live in flash (AVR).
//
// INBOUND COMMANDS (main board → node)
// -------------------------------------
// Suggested / implemented keywords (exact match, case sensitive):
//   SET_MAINTENANCE     → STATUS_MAINTENANCE (no customer play).
//   CLEAR_MAINTENANCE → STATUS_ONLINE (also useful after ERROR recovery).
//   SET_OFFLINE       → STATUS_OFFLINE (closed / unavailable).
//   CLEAR_OFFLINE     → STATUS_ONLINE.
//   PING              → emits PONG (keep-alive / wiring check).
//
// LINE FRAMING
// ------------
// Commands are newline-terminated (\n). Carriage return (\r) is ignored so CR-LF
// hosts remain compatible. Lines longer than the static buffer discard progress —
// prevents stack/RAM abuse from garbage streams.
//
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <string.h>

#include "node_config.h"
#include "node_machine_uart.h"

void machine_uart_begin() { Serial.begin(NODE_MACHINE_UART_BAUD); }

void machine_uart_game_start() { Serial.println(F("GAME_START")); }

void machine_uart_pay_fail() { Serial.println(F("PAY_FAIL")); }

void machine_uart_system_error() { Serial.println(F("SYSTEM_ERROR")); }

void machine_uart_status_online() { Serial.println(F("STATUS_ONLINE")); }

void machine_uart_status_offline() { Serial.println(F("STATUS_OFFLINE")); }

void machine_uart_status_maintenance() { Serial.println(F("STATUS_MAINTENANCE")); }

void machine_uart_status_error() { Serial.println(F("STATUS_ERROR")); }

void machine_uart_poll(proto::MachineStatus* st) {
  // Sized for longest keyword + NUL without Arduino String.
  static char line[22];
  static uint8_t pos = 0;

  if (!st) return;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;

    if (c == '\n') {
      line[pos] = '\0';
      pos = 0;

      if (strcmp(line, "SET_MAINTENANCE") == 0) {
        *st = proto::STATUS_MAINTENANCE;
      } else if (strcmp(line, "CLEAR_MAINTENANCE") == 0) {
        *st = proto::STATUS_ONLINE;
      } else if (strcmp(line, "SET_OFFLINE") == 0) {
        *st = proto::STATUS_OFFLINE;
      } else if (strcmp(line, "CLEAR_OFFLINE") == 0) {
        *st = proto::STATUS_ONLINE;
      } else if (strcmp(line, "PING") == 0) {
        Serial.println(F("PONG"));
      }
      continue;
    }

    if (pos < sizeof(line) - 1)
      line[pos++] = c;
    else
      pos = 0;
  }
}
