#pragma once
// =============================================================================
// node_reader_uart.h — human-readable framing toward RFID payment terminal
// =============================================================================
//
// WHY TEXT UART HERE (WHILE RF STAYS BINARY)
// ------------------------------------------
// The payment terminal is a dedicated module inches away — engineers can probe lines
// with any UART adapter, and field staff can recognize frames visually in logs.
// Bandwidth is negligible compared with RF duty cycles, so readability wins locally.
//
// RESPONSIBILITY SPLIT
// --------------------
// This translation layer ONLY moves bytes between MCU UART pins and main logic:
//   • NO buzzer/display logic here — those remain inside the payment terminal MCU/UI.
//
// BUFFER OVERFLOW STRATEGY
// ------------------------
// Lines longer than NODE_RFID_LINE_MAX bytes are discarded harmlessly until the next
// newline so a rogue stream cannot exhaust the single SRAM scratch buffer.
//
// =============================================================================

#include <stdint.h>

#include "node_protocol.h"

/// Poll outcomes delivered to main loop state machine.
enum : uint8_t {
  READER_EVT_NONE = 0,
  READER_EVT_SWIPE = 1,
  READER_EVT_MODE = 2,
};

void node_reader_uart_begin(void);

/// Non-blocking drain of USART0 ring buffer; returns READER_EVT_* constants.
/// On READER_EVT_SWIPE the binary card credential bytes are filled (little-endian nibble order).
/// On READER_EVT_MODE, *mode_out carries requested MachineStatus transition from terminal.
uint8_t node_reader_uart_poll(uint8_t card_id[8], uint8_t* card_id_len,
                              proto::MachineStatus* mode_out);

/// Emits RESULT,<Success|Fail|Error>,<credit> terminated with CRLF for terminal parsers.
void node_reader_uart_send_result(proto::PayResult result, uint32_t credit_remain);
