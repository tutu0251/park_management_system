#pragma once
// =============================================================================
// node_radio.h — RF24 façade: fixed 32-byte frames to/from backbone
// =============================================================================
//
// CONTRACT
// --------
// - begin(): configures channel, CRC, datarate, payload width, auto-ACK, retries,
//   opens NODE-specific RX pipe + backbone TX pipe, enters listening mode.
// - send_frame(): ALWAYS transmits proto::PAYLOAD_MAX bytes (caller supplies ≥1
//   meaningful byte; radio layer pads with zeros identically to gateway firmware).
// - try_recv(): non-blocking; consumes one RX FIFO slot if present.
//
// THREADING / REENTRANCY
// ----------------------
// Arduino loop() only — no ISR callbacks touch these APIs in this project.
//
// =============================================================================

#include <stdint.h>

#include "node_protocol.h"

bool node_radio_begin();

bool node_radio_send_frame(const uint8_t* payload, uint8_t len);

bool node_radio_try_recv(uint8_t out32[proto::PAYLOAD_MAX]);
