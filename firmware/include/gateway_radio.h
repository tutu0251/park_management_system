#pragma once
// =============================================================================
// gateway_radio.h — RF24 helpers for USB gateway role
// =============================================================================

#include <stdint.h>

bool gw_radio_setup(void);

bool gw_radio_receive(uint8_t* out32, uint8_t* pipe_out);

bool gw_radio_send_to_backbone(const uint8_t dest_addr[5], const uint8_t frame32[32]);

void gw_radio_recover(void);
