#pragma once
// =============================================================================
// backbone_radio.h — RF24 façade for the ATmega8 backbone relay
// =============================================================================
//
// CONTRACT
// --------
// - begin(): SPI + RF24 setup, channel, retries, CRC, fixed 32-byte payload,
//   opens TWO RX pipes (reader-side and gateway-side), enters listening mode.
// - try_recv(): non-blocking; reads one 32-byte packet if available and reports
//   which pipe it came from (1 = reader uplink, 2 = gateway downlink).
// - send_to(): switches the writing pipe to a five-byte address and transmits
//   one 32-byte frame with application-level retry + random backoff. Restores
//   listening mode before returning.
// - recover(): power-cycle the nRF24 and reapply settings after persistent TX
//   failures; called by the router after kRadioFailThreshold consecutive misses.
//
// SRAM BUDGET
// -----------
// One static RF24 driver instance (~ tens of bytes) plus zero firmware-side
// buffers — callers own their 32-byte frame buffer and pass it in.
//
// =============================================================================

#include <stdint.h>

bool backbone_radio_begin();

bool backbone_radio_try_recv(uint8_t out32[32], uint8_t* pipe_out);

bool backbone_radio_send_to(const uint8_t addr[5], const uint8_t frame32[32]);

void backbone_radio_recover();
