#pragma once
// =============================================================================
// node_reader.h — RFID UID extraction for SWIPE_REQ construction
// =============================================================================
//
// Typical wiring: UART RFID reader module TX → NODE_RFID_SOFT_RX_PIN (MCU RX).
// Baud NODE_RFID_UART_BAUD. See node_reader.cpp for framing rules.
//
// RETURN SEMANTICS
// ----------------
// poll_swipe() returns true at most once per completed newline-delimited UID line.
// uid_out receives big-endian hex interpretation converted to raw bytes (length in
// uid_len_out). Caller (main) passes this straight into proto::build_swipe().
//
// =============================================================================

#include <stdint.h>

void node_reader_begin();

bool node_reader_poll_swipe(uint8_t uid_out[8], uint8_t* uid_len_out);
