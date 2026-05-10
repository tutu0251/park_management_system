#pragma once
// =============================================================================
// gateway_serial.h — PC-side UART command decoder for the USB gateway MCU
// =============================================================================
//
// RESPONSIBILITIES
// ----------------
//   • Collect characters until '\n', assemble `TX,...` lines.
//   • Populate `GwPcCommand` with fields extracted from comma-separated `key=value` pairs.
//   • Never allocate heap memory; never block waiting indefinitely for a line.
//
// WHAT THIS MODULE DOES **NOT** DO
// ---------------------------------
//   • No RF I/O — see gateway_radio.cpp.
//   • No binary struct packing — see gateway_protocol.cpp + park_rf_protocol.h.
//
// =============================================================================

#include <stdint.h>

/// Parsed representation of one logical PC command after successful line decode.
///
/// kind selects downstream framing:
///   • 0 — invalid / unused (parser rejects lines that end here).
///   • 1 — payment response path → builds proto::PayRespPacked for RF TX.
///   • 2 — machine poll path      → builds proto::CheckStatusFwdPacked for RF TX.
///
/// Field usage by kind:
///   Payment (kind==1): node_id, reader_id, transaction_id, card_id/_len, pay_result,
///                      optional credit_remain if has_credit==true.
///   Poll (kind==2):    node_id (must be non-zero), reader_id, optional game_id/type_tag.
struct GwPcCommand {
  uint8_t kind;

  uint16_t node_id;
  uint16_t reader_id;

  /// Copied into proto::PayRespPacked — must match the pending swipe on the reader MCU.
  uint32_t transaction_id;

  /// Display balance / accounting snapshot relayed to payment terminal UART as RESULT line.
  uint32_t credit_remain;

  /// Numeric proto::PayResult mirror (0 Success, 1 Fail, 2 Error) after parse_event_type().
  uint8_t pay_result;

  /// If false, credit_remain is forced to zero when building PAY_RESP (still valid RF frame).
  bool has_credit;

  /// Raw credential bytes parsed from ASCII hex in `card_id=` field (≤8 bytes).
  uint8_t card_id[8];
  uint8_t card_id_len;

  /// Optional tariff/context IDs for CheckStatus polling — forwarded inside RF envelope.
  uint16_t game_id;
  uint8_t type_tag[4];
};

/// Reset serial reassembly state — call once during setup().
void gw_serial_begin(void);

/// Emit `GW<id>_ok` banner so operators know UART path is alive independent from RF bring-up.
void gw_serial_print_banner(void);

/// Non-blocking poll: returns true once a **complete** syntactically-valid command was read.
bool gw_serial_drain_command(GwPcCommand* out_cmd);

/// Emit standardized ERR line after RF TX exhaustion so hosts can script retries/logging.
void gw_serial_print_err_tx_failed(const GwPcCommand* cmd);
