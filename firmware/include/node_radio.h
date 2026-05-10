// Include guard.
#pragma once
// =============================================================================
// node_radio.h — RF24 façade for reader nodes (addresses documented in node_config.h)
// =============================================================================

#include <stdint.h>

#include "park_rf_protocol.h"

// Tiny deterministic PRNG — jitter only; not for cryptography.
uint16_t prng16(void);
void prng_seed(uint16_t seed);

// Configure channel, pipes, Shockburst width — call once from setup.
bool node_radio_begin(void);

// TX with padding to 32 B and bounded retries — returns after ACK success or exhaustion.
bool node_radio_send_frame(const uint8_t* data, uint8_t len);

// Non-blocking RX — reads one full payload if available.
bool node_radio_try_recv(uint8_t out[proto::PAYLOAD_MAX]);
