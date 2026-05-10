// =============================================================================
// main.cpp — ATmega8 reader NODE: RFID UART ↔ binary RF ↔ backbone
// =============================================================================
//
// ROLE
// ----
// Bridges UART lines from the payment terminal and fixed 32-byte Shockburst frames
// toward the backbone concentrator. This MCU never speaks USB to the billing PC; the
// gateway handles that hop after the backbone relays packets westbound.
//
// MAIN LOOP PHASES
// ----------------
// 1) Pet watchdog first — every path that blocks internally still calls wdt_reset().
// 2) Drain RF FIFO — PAY_RESP correlation, CHECK_STATUS_REQ fast replies, MSG_ERROR abort.
// 3) Poll UART — SWIPE hex payloads become SWIPE_REQ; MODE lines alter MachineStatus.
// 4) Periodic STATUS_PUSH with jitter so many cabinets do not collide on identical deadlines.
//
// SRAM RULES
// ----------
// One RX scratch (`g_rf_tx` partner `g_rf_rx`) policy from legacy layout: separate TX/RX
// staging buffers avoid tearing if a status reply is built while a payment frame arrives.
//
// =============================================================================

#include <Arduino.h>
#include <avr/wdt.h>
#include <string.h>

#include "node_config.h"
#include "node_radio.h"
#include "node_reader_uart.h"
#include "park_rf_protocol.h"

#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT WDTO_1S
#endif

namespace {

uint8_t g_rf_rx[proto::PAYLOAD_MAX];
uint8_t s_rf_tx[proto::PAYLOAD_MAX];

proto::MachineStatus g_machine_status = proto::STATUS_ONLINE;
uint32_t g_txn_id = 1;
uint32_t g_next_status_tick = 0;
uint8_t g_radio_fail_streak = 0;

/// Tracks the swipe currently awaiting PAY_RESP after Shockburst ACK of SWIPE_REQ.
/// Single-slot by design — simultaneous swipes are rejected at UART layer (`send_swipe`).
struct PendingSwipe {
  bool active;
  uint32_t txn;
  uint8_t card_len;
  uint8_t card[8];
} g_pending;

void wdt_arm(void) {
  wdt_disable();
  wdt_enable(WDT_TIMEOUT);
}

static void note_radio_tx(bool ok);

static bool rf_push_status(proto::MachineStatus st) {
  const size_t n = proto::build_status_push(st, s_rf_tx, sizeof(s_rf_tx));
  if (n == 0) return false;
  const bool tx_ok = node_radio_send_frame(s_rf_tx, static_cast<uint8_t>(n));
  note_radio_tx(tx_ok);
  return tx_ok;
}

static void set_machine_status(proto::MachineStatus st) {
  if (st == g_machine_status) return;
  g_machine_status = st;
  (void)rf_push_status(g_machine_status);
}

static void note_radio_tx(bool ok) {
  if (ok) {
    g_radio_fail_streak = 0;
    if (g_machine_status == proto::STATUS_ERROR) {
      set_machine_status(proto::STATUS_ONLINE);
    }
    return;
  }

  if (g_machine_status == proto::STATUS_ERROR) return;

  if (++g_radio_fail_streak < NODE_RADIO_FAIL_THRESHOLD) return;

  set_machine_status(proto::STATUS_ERROR);
}

static void handle_check_status(void) {
  const size_t n = proto::build_status_response(g_machine_status, s_rf_tx, sizeof(s_rf_tx));
  if (n == 0) return;
  const bool ok = node_radio_send_frame(s_rf_tx, static_cast<uint8_t>(n));
  note_radio_tx(ok);
}

/// Ensures PAY_RESP belongs to our outstanding swipe (txn + credential + lane IDs).
static bool pending_matches(const proto::PayRespPacked* pr) {
  if (!g_pending.active) return false;
  if (pr->transaction_id != g_pending.txn) return false;
  if (pr->node_id != node_cfg::kNodeId || pr->reader_id != node_cfg::kReaderId) return false;
  if (pr->card_id_len != g_pending.card_len) return false;
  return memcmp(pr->card_id, g_pending.card, g_pending.card_len) == 0;
}

/// Converts correlated PAY_RESP into UART RESULT line for payment terminal firmware.
static void on_pay_response(const proto::PayRespPacked* pr) {
  if (!pending_matches(pr)) return;

  g_pending.active = false;

  proto::PayResult r = static_cast<proto::PayResult>(pr->event_type);
  if (r > proto::PAY_ERROR) r = proto::PAY_ERROR;

  node_reader_uart_send_result(r, pr->credit_remain);
}

/// MSG_ERROR aborts authorization wait — treats as hard fault visible to guest-facing UI.
static void on_mesh_error_packet(void) {
  if (!g_pending.active) return;
  g_pending.active = false;
  node_reader_uart_send_result(proto::PAY_ERROR, 0);
}

/// Non-blocking RF decode — safe to call from loop while UART polls interleave.
static void process_radio_frame(uint8_t* frame32) {
  proto::PayRespPacked pay{};
  bool chk = false;
  bool remote_err = false;

  if (!proto::parse_rx(frame32, proto::PAYLOAD_MAX, &pay, &chk, &remote_err)) return;

  if (chk) {
    handle_check_status();
    return;
  }

  if (remote_err) {
    on_mesh_error_packet();
    return;
  }

  if (frame32[0] == proto::PAY_RESP) {
    on_pay_response(&pay);
  }
}

/// Bounded wait that still services CHECK_STATUS polls + watchdog between polls.
static bool wait_for_pay_response(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;

  while (static_cast<int32_t>(millis() - deadline) < 0) {
    wdt_reset();

    if (!node_radio_try_recv(g_rf_rx)) continue;

    proto::PayRespPacked pay{};
    bool chk = false;
    bool remote_err = false;
    if (!proto::parse_rx(g_rf_rx, proto::PAYLOAD_MAX, &pay, &chk, &remote_err)) continue;

    if (chk) {
      handle_check_status();
      continue;
    }

    if (remote_err) {
      on_mesh_error_packet();
      return false;
    }

    if (g_rf_rx[0] == proto::PAY_RESP) {
      if (pending_matches(&pay)) {
        on_pay_response(&pay);
        return true;
      }
      continue;
    }
  }

  if (g_pending.active) {
    g_pending.active = false;
    node_reader_uart_send_result(proto::PAY_ERROR, 0);
  }
  return false;
}

/// Happy path: package UID → SWIPE_REQ → RF TX → block until PAY_RESP or timeout.
static bool send_swipe(const uint8_t* uid, uint8_t uid_len) {
  if (g_machine_status != proto::STATUS_ONLINE) {
    node_reader_uart_send_result(proto::PAY_ERROR, 0);
    return false;
  }

  if (g_pending.active) return false;

  const uint32_t txn = g_txn_id++;

  size_t out_len = 0;
  if (!proto::build_swipe(uid, uid_len, node_cfg::kNodeId, node_cfg::kReaderId, txn,
                          node_cfg::kDefaultGameId, node_cfg::kDefaultTypeId, proto::EVT_SWIPE,
                          s_rf_tx, sizeof(s_rf_tx), &out_len)) {
    node_reader_uart_send_result(proto::PAY_ERROR, 0);
    return false;
  }

  const bool ok = node_radio_send_frame(s_rf_tx, static_cast<uint8_t>(out_len));
  note_radio_tx(ok);
  if (!ok) {
    node_reader_uart_send_result(proto::PAY_ERROR, 0);
    return false;
  }

  g_pending.active = true;
  g_pending.txn = txn;
  g_pending.card_len = uid_len;
  memcpy(g_pending.card, uid, uid_len);

  (void)wait_for_pay_response(NODE_PAY_WAIT_MS);
  return true;
}

/// STATUS_PUSH cadence with jitter so fleets of cabinets avoid synchronized RF collisions.
static void periodic_status_push(uint32_t now) {
  if (static_cast<int32_t>(now - g_next_status_tick) < 0) return;

  g_next_status_tick = now + NODE_STATUS_PUSH_MS + (static_cast<uint32_t>(prng16()) & 0x1FFU);

  (void)rf_push_status(g_machine_status);
}

}  // namespace

void setup(void) {
  node_reader_uart_begin();

  prng_seed(static_cast<uint16_t>(millis() & 0xFFFFU));

  delay(50);

  wdt_arm();

  g_pending.active = false;

  if (!node_radio_begin()) {
    g_machine_status = proto::STATUS_ERROR;
  } else {
    g_machine_status = proto::STATUS_ONLINE;
  }

  (void)rf_push_status(g_machine_status);

  g_next_status_tick = millis() + NODE_STATUS_PUSH_MS;
}

void loop(void) {
  wdt_reset();

  const uint32_t now = millis();

  // Drain entire Shockburst RX FIFO — prevents backlog when UART work stalls briefly.
  while (node_radio_try_recv(g_rf_rx)) process_radio_frame(g_rf_rx);

  uint8_t card[8];
  uint8_t card_len = 0;
  proto::MachineStatus mode = g_machine_status;

  const uint8_t evt = node_reader_uart_poll(card, &card_len, &mode);
  if (evt == READER_EVT_MODE) {
    set_machine_status(mode);
  } else if (evt == READER_EVT_SWIPE) {
    (void)send_swipe(card, card_len);
  }

  periodic_status_push(now);
}
