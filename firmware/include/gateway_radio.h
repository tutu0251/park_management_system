// Include guard — safe to include from main and tests without duplicate symbols.
#pragma once
// =============================================================================
// gateway_radio.h — RF24 helpers for USB gateway role
// =============================================================================

// Pointer parameters use explicit uint8_t spans — fixed 32-byte payloads.
#include <stdint.h>

// One-time SPI + RF24 pipe setup — call from setup() before RX/TX.
bool gw_radio_setup(void);

// Non-blocking RX: copies one 32-byte payload if FIFO non-empty; optional pipe index.
bool gw_radio_receive(uint8_t* out32, uint8_t* pipe_out);

// TX with retries + jitter — opens `dest_addr` as writing pipe for duration of attempts.
bool gw_radio_send_to_backbone(const uint8_t dest_addr[5], const uint8_t frame32[32]);

// Power-cycle RF and reopen default pipes after repeated ACK failures.
void gw_radio_recover(void);
