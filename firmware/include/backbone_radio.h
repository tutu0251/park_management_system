// Include guard.
#pragma once
// =============================================================================
// backbone_radio.h — RF24 transport for the backbone relay role
// =============================================================================

// Fixed 32-byte Shockburst payloads everywhere.
#include <stdint.h>

// SPI init, dual RX pipes (readers + gateway), default TX pipe toward ADDR_SERVER.
bool backbone_radio_begin(void);

// Non-blocking RX: fills `out` when FIFO ready; optional RF24 pipe index for tracing.
bool backbone_radio_try_recv(uint8_t out[32], uint8_t* pipe_out);

// Uplink relay toward gateway (ADDR_SERVER) with software retries + backoff.
bool backbone_radio_send_to_server(const uint8_t frame[32]);

// Downlink toward cabinet radio NODE{n} — node_id must be 1..9 for current ASCII scheme.
bool backbone_radio_send_to_reader(uint16_t node_id, const uint8_t frame[32]);

// Power-cycle RF and reopen pipes after sustained failures.
void backbone_radio_recover(void);
