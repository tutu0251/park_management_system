#pragma once
// =============================================================================
// backbone_radio.h — RF24 transport for the backbone relay role
// =============================================================================

#include <stdint.h>

bool backbone_radio_begin(void);

bool backbone_radio_try_recv(uint8_t out[32], uint8_t* pipe_out);

bool backbone_radio_send_to_server(const uint8_t frame[32]);

bool backbone_radio_send_to_reader(uint16_t node_id, const uint8_t frame[32]);

void backbone_radio_recover(void);
