#pragma once
// =============================================================================
// gateway_radio.h — RF24 init, fixed 32-byte RX/TX, backoff retries
// =============================================================================

#include <stdint.h>

bool gw_radio_setup();
bool gw_radio_receive(uint8_t* out32, uint8_t* pipe_out);
bool gw_radio_send_to_backbone(const uint8_t dest_addr[5], const uint8_t frame32[32]);
void gw_radio_recover();
