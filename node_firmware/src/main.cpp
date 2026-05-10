// =============================================================================
// main.cpp — ATmega8 reader node: RFID UART ↔ binary RF ↔ backbone
// =============================================================================
//
// SYSTEM ROLE (WHAT THIS FIRMWARE IS / IS NOT)
// --------------------------------------------
// This image runs on **RFID reader nodes** only. It bridges:
//   • UART text framing from the RFID reader / payment terminal, and
//   • Compact binary Shockburst frames toward the **backbone** concentrator.
//
// It does **not** talk to the game machine mainboard, billing PC, or gateway stack
// directly — those responsibilities live in other hardware blocks.
//
// DATA PLANES
// -----------
// 1) UART ingress  — commands such as `SWIPE,<hex>` and optional `MODE,<status>`.
// 2) RF uplink     — SWIPE_REQ carrying transaction_id + credential snapshot.
// 3) RF downlink   — PAY_RESP / CHECK_STATUS_REQ / optional MSG_ERROR.
// 4) Status plane  — STATUS_PUSH heartbeats + immediate pushes on transitions.
//
// COOPERATIVE “MULTITASKING” (NO RTOS HERE)
// -----------------------------------------
// loop() never blocks except inside pay_response_wait(), which still polls radio,
// explicitly kicks the watchdog, and honors backbone CHECK_STATUS while waiting.
// Everything else is split into short poll phases suitable for 1 KiB SRAM devices.
//
// ATmega8 MEMORY DISCIPLINE (WHY THIS MATTERS)
// --------------------------------------------
// Rough budget: ~1 KiB SRAM + ~8 KiB flash (minus bootloader). There is **no MMU**,
// **no guard page**, and **no safe heap allocator** for long-run fragmentation control.
// Rules we follow here:
//   • No `String`, STL containers, `malloc/new`, recursion, or giant stack arrays.
//   • One RF RX scratch (`g_rf_rx`) and one RF TX scratch (`s_rf_tx`) reused everywhere.
//   • Transaction context stored in a fixed `PendingSwipe` struct (no dynamic growth).
//
// WATCHDOG PHILOSOPHY
// -------------------
// If SPI hangs, RF IRQ storms, or logic regresses into an accidental infinite spin,
// the watchdog resets us back to a known safe state instead of bricking a revenue lane.
// Any newly added blocking path MUST call `wdt_reset()` inside tight loops.
//
// TRANSACTION_ID CORRELATION (PREVENTS RESPONSE MISMATCH)
// -------------------------------------------------------
// Servers can be slower than radio ACKs. Responses may arrive **after** additional RF
// traffic or **duplicated** by retries. We generate monotonic `transaction_id` values per
// SWIPE attempt and require PAY_RESP to echo **transaction_id + card_id bytes + ids**
// before emitting UART `RESULT,*` — stale PAY_RESP frames are ignored intentionally.
//
// =============================================================================

#include <Arduino.h>
#include <avr/wdt.h>
#include <string.h>

#include "node_config.h"
#include "node_protocol.h"
#include "node_radio.h"
#include "node_reader_uart.h"

#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT WDTO_1S
#endif

namespace {

/// Shockburst window dedicated to **RX** paths (never reused while parsing still needed).
uint8_t g_rf_rx[proto::PAYLOAD_MAX];

/// Shockburst window dedicated to **TX** builds (STATUS replies, SWIPE frames, ...).
/// Keeping TX separate from `g_rf_rx` prevents subtle corruption if we answer a poll
/// while simultaneously receiving PAY_RESP during the payment wait window.
uint8_t s_rf_tx[proto::PAYLOAD_MAX];

proto::MachineStatus g_machine_status = proto::STATUS_ONLINE;

/// Monotonic swipe handle generator — wraps legally at UINT32_MAX (still unique enough in
/// combined practice with server-side correlation windows).
uint32_t g_txn_id = 1;

/// Next STATUS_PUSH deadline — jittered using `prng16()` to avoid synchronized mesh bursts.
uint32_t g_next_status_tick = 0;

/// Consecutive RF TX failures — crossing NODE_RADIO_FAIL_THRESHOLD elevates ERROR status.
uint8_t g_radio_fail_streak = 0;

/// Tracks one outstanding authorization attempt after Shockburst ACK of SWIPE_REQ.
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

/// Applies a new MachineStatus and attempts immediate RF notification (STATUS_PUSH).
static void set_machine_status(proto::MachineStatus st) {
  if (st == g_machine_status) return;
  g_machine_status = st;
  (void)rf_push_status(g_machine_status);
}

/// Central RF reliability hook — success clears streak; failures escalate toward ERROR.
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

static bool pending_matches(const proto::PayRespPacked* pr) {
  if (!g_pending.active) return false;
  if (pr->transaction_id != g_pending.txn) return false;
  if (pr->node_id != node_cfg::kNodeId || pr->reader_id != node_cfg::kReaderId) return false;
  if (pr->card_id_len != g_pending.card_len) return false;
  return memcmp(pr->card_id, g_pending.card, g_pending.card_len) == 0;
}

static void on_pay_response(const proto::PayRespPacked* pr) {
  if (!pending_matches(pr)) return;

  g_pending.active = false;

  proto::PayResult r = static_cast<proto::PayResult>(pr->event_type);
  if (r > proto::PAY_ERROR) r = proto::PAY_ERROR;

  /// UART mirrors billing semantics — credit remains authoritative from backbone math.
  node_reader_uart_send_result(r, pr->credit_remain);
}

/// Backbone issued MSG_ERROR — abort local authorization wait if any (gateway-side fault).
static void on_mesh_error_packet(void) {
  if (!g_pending.active) return;
  g_pending.active = false;
  node_reader_uart_send_result(proto::PAY_ERROR, 0);
}

static void process_radio_frame(uint8_t* frame32) {
  proto::PayRespPacked pay{};
  bool chk = false;
  bool remote_err = false;

  if (!proto::parse_rx(frame32, proto::PAYLOAD_MAX, &pay, &chk, &remote_err)) return;

  if (chk) {
    /// Gateway/billing stacks poll machines by injecting CHECK_STATUS_REQ while gameplay
    /// continues elsewhere — respond promptly so dashboards differentiate **Maintenance**
    /// (intentionally paused by staff) from **Offline** (power/network loss) vs **Error**
    /// (RF/server pathology).
    handle_check_status();
    return;
  }

  if (remote_err) {
    on_mesh_error_packet();
    return;
  }

  if (frame32[0] == proto::PAY_RESP) {
    /// PAY_RESP always copies into `pay`, but `on_pay_response()` additionally validates the
    /// mirrored credential/txn tuple — random stray ACK payloads cannot enable credits.
    on_pay_response(&pay);
  }
}

/// Blocking wait that **still services mesh housekeeping** (poll + watchdog).
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
    /// Timeout ≠ decline — it means **no correlated PAY_RESP**. Terminal shows Error so
    /// staff can investigate RF/backbone rather than blaming the guest card incorrectly.
    node_reader_uart_send_result(proto::PAY_ERROR, 0);
  }
  return false;
}

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

/// Periodic heartbeat so dashboards detect silent failures even when gameplay stops.
static void periodic_status_push(uint32_t now) {
  if (static_cast<int32_t>(now - g_next_status_tick) < 0) return;

  /// Pseudo-random jitter spreads STATUS_PUSH edges — reduces repeated collisions when many
  /// nodes share identical firmware timers after simultaneous reset.
  g_next_status_tick = now + NODE_STATUS_PUSH_MS + (static_cast<uint32_t>(prng16()) & 0x1FFU);

  (void)rf_push_status(g_machine_status);
}

}  // namespace

void setup(void) {
  node_reader_uart_begin();

  /// Stir PRNG seed so identical HEX files still exhibit diverse backoff after field power cycles.
  prng_seed(static_cast<uint16_t>(millis() & 0xFFFFU));

  delay(50);

  wdt_arm();

  g_pending.active = false;

  if (!node_radio_begin()) {
    g_machine_status = proto::STATUS_ERROR;
  } else {
    g_machine_status = proto::STATUS_ONLINE;
  }

  /// Boot-time heartbeat — `set_machine_status()` skips duplicate pushes, so we explicitly
  /// announce whichever state boot produced (healthy Online vs radio init Error).
  (void)rf_push_status(g_machine_status);

  g_next_status_tick = millis() + NODE_STATUS_PUSH_MS;
}

void loop(void) {
  /// Watchdog pet FIRST — keeps worst-case deadline predictable even if later stages regress.
  wdt_reset();

  const uint32_t now = millis();

  /// -------------------------------------------------------------------------
  /// RF ingress pass (non-blocking Shockburst drain)
  /// -------------------------------------------------------------------------
  /// Backbone may enqueue multiple frames while we were servicing UART or timers.
  /// Processing the FIFO until empty mimics “run-to-completion” for RF events without
  /// permanently allocating per-packet RAM — each decode reuses `g_rf_rx`.
  ///
  /// Routing recap:
  ///   • CHECK_STATUS_REQ → immediate STATUS_RESP (still compatible with watchdog cadence).
  ///   • PAY_RESP         → correlated against `g_pending` before touching UART.
  ///   • MSG_ERROR        → aborts any dangling authorization wait as `RESULT,Error,0`.
  /// -------------------------------------------------------------------------
  while (node_radio_try_recv(g_rf_rx)) process_radio_frame(g_rf_rx);

  /// -------------------------------------------------------------------------
  /// UART ingress pass (payment terminal dialog)
  /// -------------------------------------------------------------------------
  /// Typical happy path: `SWIPE,A1B2C3D4` triggers SWIPE_REQ uplink + blocking wait for
  /// PAY_RESP.  Operational path: `MODE,MAINTENANCE` raises STATUS_MAINTENANCE so park IT
  /// can silence a cabinet without physically disconnecting RF (servers see STATUS_PUSH).
  /// -------------------------------------------------------------------------
  uint8_t card[8];
  uint8_t card_len = 0;
  proto::MachineStatus mode = g_machine_status;

  const uint8_t evt = node_reader_uart_poll(card, &card_len, &mode);
  if (evt == READER_EVT_MODE) {
    set_machine_status(mode);
  } else if (evt == READER_EVT_SWIPE) {
    (void)send_swipe(card, card_len);
  }

  /// -------------------------------------------------------------------------
  /// Background timers / heartbeats
  /// -------------------------------------------------------------------------
  /// Even when nobody taps cards, operations teams expect periodic STATUS_PUSH frames to
  /// prove link health.  Jitter (`prng16`) prevents many nodes from transmitting numeric
  /// deadlines simultaneously after a brownout-induced reboot wave.
  /// -------------------------------------------------------------------------
  periodic_status_push(now);
}
