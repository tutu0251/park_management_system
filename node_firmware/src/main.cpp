// =============================================================================
// main.cpp — ATmega8 reader node: RFID swipe → payment RF ↔ backbone
// =============================================================================
//
// ROLE IN THE PARK SYSTEM
// -----------------------
// This MCU sits beside the game machine. It does NOT talk USB/Ethernet to a PC.
// Flow (see repo backbone_firmware):
//
//   [Game main board] <--UART--> [This reader node] <--nRF24--> [Backbone] ...
//
// The reader sends billing requests (SWIPE_REQ) toward the backbone listen
// address configured as GWAY1 (see node_config.h). The backbone relays to the
// billing path and sends PAY_RESP back to this node's private pipe address
// NODE{n} (e.g. NODE1 .. NODE9). CHECK_STATUS_REQ may arrive anytime; we reply
// with STATUS_RESP.
//
// ATmega8 CONSTRAINTS (why the code looks like this)
// ----------------------------------------------------
// - ~1 KiB SRAM: avoid large stack frames; reuse one 32-byte RF buffer; no heap.
// - No Arduino String / STL / printf chains in critical paths.
// - Single hardware USART: reserved for the game machine; RFID uses SoftwareSerial
//   (see node_reader.cpp).
// - Watchdog: any long wait (e.g. PAY_RESP) must call wdt_reset() frequently.
//
// MAIN LOOP ORDERING (rough priority)
// -----------------------------------
// 1) Pet watchdog — keeps chip from resetting if loop stalls elsewhere.
// 2) Poll game UART — operator commands may change MachineStatus.
// 3) Drain all pending RF packets — backbone may send PAY_RESP or polls while we
//    are otherwise busy; inner while() avoids backlog growth.
// 4) Poll RFID UART — non-blocking; one swipe produces one payment attempt.
// 5) Periodic STATUS_PUSH — heartbeat so upstream sees Online/Maintenance/etc.
//
// PAYMENT CORRELATION MODEL
// ------------------------
// The binary PAY_RESP frame does not repeat transaction_id. We therefore treat
// payment as strictly sequential: after a successful SWIPE_REQ TX we set
// g_waiting_pay; the next PAY_RESP we accept clears it. Stale PAY_RESP outside
// that window is ignored (defensive against RF duplicates/glitches).
//
// RADIO FAILURE POLICY
// --------------------
// node_radio_send_frame() returning false counts toward g_radio_fail_streak.
// After NODE_RADIO_FAIL_THRESHOLD consecutive failures we latch STATUS_ERROR,
// notify the game UART, and attempt an immediate STATUS_PUSH over RF (best-effort).
// While already in STATUS_ERROR we stop accumulating streak (wrap-safe).
//
// =============================================================================

#include <Arduino.h>
#include <avr/wdt.h>
#include <string.h>

#include "node_config.h"
#include "node_machine_uart.h"
#include "node_protocol.h"
#include "node_radio.h"
#include "node_reader.h"

// ATmega8 watchdog prescaler options differ from ATmega328; WDTO_1S is a safe
// default with MiniCore. Increase only if you prove loop()+worst-case waits
// always complete faster than the timeout under RF contention.
#ifndef WDT_TIMEOUT
#define WDT_TIMEOUT WDTO_1S
#endif

namespace {

// --- RF scratch buffer (single shared 32-byte window) ------------------------
// Every on-air ShockBurst payload is fixed at 32 bytes (proto::PAYLOAD_MAX).
// We reuse this buffer for: building SWIPE_REQ, RX parse paths in loop(), and
// blocking wait during PAY_RESP. Single-threaded Arduino loop makes this safe.
uint8_t g_rf_frame[proto::PAYLOAD_MAX];

// --- Operator / fault visibility --------------------------------------------
// MachineStatus drives:
//   - STATUS_RESP answers to CHECK_STATUS_REQ (binary enum over RF).
//   - STATUS_PUSH periodic / event uplinks (two-byte wire format, padded to 32).
//   - ASCII lines to the game board via machine_uart_* helpers.
proto::MachineStatus g_machine_status = proto::STATUS_ONLINE;

// Last MachineStatus we successfully announced with STATUS_PUSH (RF). Used if
// we later want de-duplication logic; periodic_push updates when TX succeeds.
proto::MachineStatus g_last_pushed_rf = proto::STATUS_ONLINE;

// Monotonic swipe correlation id embedded in SWIPE_REQ (server may log/idempotent).
uint32_t g_txn_id = 1;

// millis() deadline for the next periodic STATUS_PUSH (NODE_STATUS_PUSH_MS).
uint32_t g_next_status_tick = 0;

// Consecutive failed node_radio_send_frame() attempts while not already ERROR.
uint8_t g_radio_fail_streak = 0;

// True between successful SWIPE_REQ transmission and matching PAY_RESP / timeout.
bool g_waiting_pay = false;

// Arm MCU watchdog after optional configuration window in setup().
void wdt_arm() {
  wdt_disable();
  wdt_enable(WDT_TIMEOUT);
}

// Zero-pad logical payloads shorter than 32 bytes so the RF layer always emits
// full-width frames (matches backbone radio_send_to behaviour).
void pad32(uint8_t* frame, uint8_t payload_len) {
  if (payload_len >= proto::PAYLOAD_MAX) return;
  memset(frame + payload_len, 0, proto::PAYLOAD_MAX - payload_len);
}

// Track RF TX outcome for failure escalation. Successful TX clears streak.
// Failure path: count toward threshold; crossing threshold raises STATUS_ERROR,
// tells the game board, and tries to push STATUS_ERROR upstream (so operators
// see the fault remotely). That emergency push uses send_frame directly here to
// avoid recursion through rf_push_status → note_radio_tx again.
void note_radio_tx(bool ok) {
  if (ok) {
    g_radio_fail_streak = 0;
    return;
  }

  // Already in fault state: do not keep incrementing forever (uint8 wrap).
  if (g_machine_status == proto::STATUS_ERROR) return;

  if (++g_radio_fail_streak < NODE_RADIO_FAIL_THRESHOLD) return;

  g_machine_status = proto::STATUS_ERROR;
  machine_uart_status_error();

  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_push(g_machine_status, buf, sizeof(buf));
  if (n == 0) return;

  const bool push_ok = node_radio_send_frame(buf, static_cast<uint8_t>(n));
  if (push_ok) {
    g_last_pushed_rf = g_machine_status;
    g_radio_fail_streak = 0;
  }
}

// Proactive STATUS_PUSH [type][status] padded to 32 bytes toward backbone.
bool rf_push_status(proto::MachineStatus st) {
  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_push(st, buf, sizeof(buf));
  if (n == 0) return false;
  const bool ok = node_radio_send_frame(buf, static_cast<uint8_t>(n));
  note_radio_tx(ok);
  return ok;
}

// Mirror MachineStatus to the game controller as a human-readable line ending in \n.
void notify_machine_uart_status(proto::MachineStatus st) {
  switch (st) {
    case proto::STATUS_ONLINE:
      machine_uart_status_online();
      break;
    case proto::STATUS_MAINTENANCE:
      machine_uart_status_maintenance();
      break;
    case proto::STATUS_OFFLINE:
      machine_uart_status_offline();
      break;
    case proto::STATUS_ERROR:
      machine_uart_status_error();
      break;
    default:
      break;
  }
}

// Backbone sent CHECK_STATUS_REQ (single type byte). Reply with STATUS_RESP
// carrying current g_machine_status so dashboards/poller logic stay in sync.
void handle_check_status() {
  uint8_t buf[proto::PAYLOAD_MAX];
  const size_t n = proto::build_status_response(g_machine_status, buf, sizeof(buf));
  if (n == 0) return;
  const bool ok = node_radio_send_frame(buf, static_cast<uint8_t>(n));
  note_radio_tx(ok);
}

// Apply PAY_RESP body only when we are actively expecting it after our swipe.
// credit_after_cents is reserved for future UI/credit displays on the machine.
bool handle_pay_payload(proto::PayResult pr, uint32_t credit_cents) {
  (void)credit_cents;
  if (!g_waiting_pay) return false;

  g_waiting_pay = false;

  if (pr == proto::PAY_OK) {
    machine_uart_game_start();
  } else if (pr == proto::PAY_FAIL) {
    machine_uart_pay_fail();
  } else {
    machine_uart_system_error();
  }
  return true;
}

// Decode one 32-byte RX frame from backbone/server relay. Interleaved CHECK_STATUS
// requests during a swipe wait are handled inline so polls do not block billing.
void process_radio_frame(uint8_t* frame32) {
  proto::PayResult pr = proto::PAY_ERROR;
  uint32_t credit = 0;
  bool chk = false;

  if (!proto::parse_rx(frame32, proto::PAYLOAD_MAX, &pr, &credit, &chk)) return;

  if (chk) {
    handle_check_status();
    return;
  }

  if (frame32[0] == proto::PAY_RESP) (void)handle_pay_payload(pr, credit);
}

// Block up to timeout_ms for PAY_RESP after SWIPE_REQ. Keeps servicing CHECK_STATUS
// while waiting. On timeout: clear waiting flag and signal SYSTEM_ERROR to game.
bool wait_for_pay_response(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;

  while (static_cast<int32_t>(millis() - deadline) < 0) {
    wdt_reset();

    if (!node_radio_try_recv(g_rf_frame)) continue;

    proto::PayResult pr = proto::PAY_ERROR;
    uint32_t credit = 0;
    bool chk = false;
    if (!proto::parse_rx(g_rf_frame, proto::PAYLOAD_MAX, &pr, &credit, &chk)) continue;

    if (chk) {
      handle_check_status();
      continue;
    }

    if (g_rf_frame[0] == proto::PAY_RESP) {
      (void)handle_pay_payload(pr, credit);
      return true;
    }
  }

  if (g_waiting_pay) {
    g_waiting_pay = false;
    machine_uart_system_error();
  }
  return false;
}

// Full swipe pipeline: gate on ONLINE, pack SWIPE_REQ, TX padded 32 bytes, wait.
bool send_swipe(const uint8_t* uid, uint8_t uid_len) {
  if (g_machine_status != proto::STATUS_ONLINE) {
    machine_uart_pay_fail();
    return false;
  }

  const uint32_t txn = g_txn_id++;

  size_t swipe_len = 0;
  if (!proto::build_swipe(uid, uid_len, node_cfg::kNodeId, node_cfg::kReaderId, txn,
                          node_cfg::kDefaultGameId, g_rf_frame, sizeof(g_rf_frame), &swipe_len)) {
    machine_uart_system_error();
    return false;
  }
  pad32(g_rf_frame, static_cast<uint8_t>(swipe_len));

  const bool ok = node_radio_send_frame(g_rf_frame, proto::PAYLOAD_MAX);
  note_radio_tx(ok);
  if (!ok) {
    machine_uart_system_error();
    return false;
  }

  g_waiting_pay = true;
  (void)wait_for_pay_response(NODE_PAY_WAIT_MS);
  return true;
}

// Time-based STATUS_PUSH heartbeat (examples: Online / Maintenance / Error ...).
void periodic_status_push(uint32_t now) {
  if (static_cast<int32_t>(now - g_next_status_tick) < 0) return;
  g_next_status_tick = now + NODE_STATUS_PUSH_MS;

  if (rf_push_status(g_machine_status)) g_last_pushed_rf = g_machine_status;
}

}  // namespace

void setup() {
  // Bring up bidirectional links before RF (game board may boot-log commands).
  machine_uart_begin();
  node_reader_begin();

  delay(50);
  wdt_arm();

  if (!node_radio_begin()) {
    // Cannot join RF mesh — surface fault locally and try to inform backbone (may fail).
    g_machine_status = proto::STATUS_ERROR;
    notify_machine_uart_status(g_machine_status);
    (void)rf_push_status(g_machine_status);
  } else {
    notify_machine_uart_status(proto::STATUS_ONLINE);
    if (rf_push_status(proto::STATUS_ONLINE)) g_last_pushed_rf = proto::STATUS_ONLINE;
  }

  g_next_status_tick = millis() + NODE_STATUS_PUSH_MS;
}

void loop() {
  wdt_reset();

  const uint32_t now = millis();

  // Game operator UART may transition maintenance/offline; mirror + RF push on edge.
  const proto::MachineStatus before = g_machine_status;
  machine_uart_poll(&g_machine_status);
  if (g_machine_status != before) {
    if (g_machine_status == proto::STATUS_ONLINE) g_radio_fail_streak = 0;
    notify_machine_uart_status(g_machine_status);
    if (rf_push_status(g_machine_status)) g_last_pushed_rf = g_machine_status;
  }

  while (node_radio_try_recv(g_rf_frame)) process_radio_frame(g_rf_frame);

  uint8_t uid[8];
  uint8_t uid_len = 0;
  if (node_reader_poll_swipe(uid, &uid_len)) (void)send_swipe(uid, uid_len);

  periodic_status_push(now);
}
