#pragma once
// =============================================================================
// node_radio.h — RF24 façade for reader nodes (addresses documented in node_config.h)
// =============================================================================

#include <stdint.h>

#include "park_rf_protocol.h"

uint16_t prng16(void);
void prng_seed(uint16_t seed);

bool node_radio_begin(void);

bool node_radio_send_frame(const uint8_t* data, uint8_t len);

bool node_radio_try_recv(uint8_t out[proto::PAYLOAD_MAX]);
