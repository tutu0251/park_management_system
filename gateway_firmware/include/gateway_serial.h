#pragma once
// =============================================================================
// gateway_serial.h — line RX from UART + lightweight TX, command parser
// =============================================================================

#include <stdint.h>

struct GwPcCommand {
  uint8_t kind;  // 0 invalid, 1 payment result, 2 check status
  uint16_t node_id;
  uint16_t reader_id;
  uint32_t transaction_id;
  uint32_t credit_remain;
  uint8_t pay_result;  // proto::PayResult when kind==1
  bool has_credit;
};

void gw_serial_begin();
void gw_serial_print_banner();
bool gw_serial_drain_command(GwPcCommand* out_cmd);
void gw_serial_print_err_tx_failed(const GwPcCommand* cmd);
