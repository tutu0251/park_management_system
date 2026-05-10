#pragma once
// =============================================================================
// gateway_protocol.h — bridge between human-readable UART telemetry and binary RF structs
// =============================================================================
//
// SPLIT OF DUTIES vs park_rf_protocol.h
// -------------------------------------
//   • park_rf_protocol.h defines **on-air memory layouts** shared by node/backbone/gateway.
//   • This header adds gateway-only helpers:
//       - CSV printing for frames arriving from the backbone (PC-friendly logs).
//       - Packing PC commands (`GwPcCommand`) into TX buffers for RF24 writes.
//
// All multi-byte integers follow AVR little-endian layout — memcpy into structs is safe.
//
// =============================================================================

#include <stdint.h>

#include "park_rf_protocol.h"

struct GwPcCommand;

namespace proto {

/// Sanity-check a SWIPE_REQ before emitting ASCII telemetry (length + opcode guards).
bool swipe_wire_ok(const uint8_t* frame32);

/// Pretty-print SWIPE_REQ as `RX,...` CSV line for host parsers / ELK dashboards.
void serial_print_rx_swipe(const uint8_t* frame32);

/// Pretty-print STATUS_PUSH / STATUS_RESP into stable `RX,...` columns (some placeholders).
void serial_print_rx_status(const uint8_t* frame32, uint8_t msg_type);

/// Construct PAY_RESP binary payload + zero pad to full 32-byte Shockburst width.
bool build_pay_frame(const GwPcCommand* cmd, uint8_t* out32);

/// Construct CHECK_STATUS routing envelope + zero pad — backbone inspects node_id field.
bool build_check_status_frame(const GwPcCommand* cmd, uint8_t* out32);

}  // namespace proto
