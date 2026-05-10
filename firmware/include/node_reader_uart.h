// Include guard.
#pragma once
// =============================================================================
// node_reader_uart.h — text framing between MCU and payment terminal USART
// =============================================================================

#include <stdint.h>

#include "park_rf_protocol.h"

// Events returned from cooperative poll — drives main.cpp state machine.
enum : uint8_t {
  READER_EVT_NONE = 0,
  READER_EVT_SWIPE = 1,
  READER_EVT_MODE = 2,
};

void node_reader_uart_begin(void);

// Consumes available serial bytes; returns first completed-line event this call (often NONE).
uint8_t node_reader_uart_poll(uint8_t card_id[8], uint8_t* card_id_len,
                              proto::MachineStatus* mode_out);

// Emits `RESULT,...` line toward payment terminal after PAY_RESP correlation.
void node_reader_uart_send_result(proto::PayResult result, uint32_t credit_remain);
