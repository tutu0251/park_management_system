#pragma once
// =============================================================================
// node_radio.h — thin RF24 façade for reader nodes (SPI + Shockburst policy)
// =============================================================================
//
// WHY CE AND CSN PINS EXIST
// -------------------------
// nRF24L01+ is controlled over SPI for registers/FIFOs, plus two GPIO helpers:
//   • CE  — enables/disables the RF front-end for RX/TX sequencing.
//   • CSN — SPI chip-select (active low) framing individual SPI transactions.
//
// WHY SPI (NOT UART/I2C HERE)
// ---------------------------
// The Nordic chip exposes a high-speed peripheral slave SPI interface suitable for
// precise FIFO pumping at low CPU cost — critical on 8-bit MCUs serving tight loops.
//
// WHY STABLE 3.3 V MATTERS
// ------------------------
// The RF PA/LNA and PLL are sensitive to brownouts; unreliable supply collapses link
// margin and produces “phantom” failures that look like software bugs. Decouple RF rail.
//
// ROUTING MODEL (reader ↔ backbone)
// ---------------------------------
// TX pipe destination : ADDR_GW_NODE — backbone’s listening address for uplink.
// RX pipe 1 address   : NODE{n} unique suffix — backbone targets per-node downlink.
//
// =============================================================================

#include <stdint.h>

#include "node_protocol.h"

/// 16-bit xorshift PRNG — tiny state, no libc entropy dependency (see .cpp comments).
uint16_t prng16(void);

/// Seed PRNG once after reset (optional entropy mix-in from millis LSB).
void prng_seed(uint16_t seed);

bool node_radio_begin(void);

/// Sends exactly proto::PAYLOAD_MAX bytes (pad before call). Applies Shockburst TX plus
/// application-level retries with randomized inter-attempt delay (collision avoidance).
bool node_radio_send_frame(const uint8_t* data, uint8_t len);

/// Non-blocking RX drain helper; copies one 32-byte FIFO slot if available.
bool node_radio_try_recv(uint8_t out[proto::PAYLOAD_MAX]);
