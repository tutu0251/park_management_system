// =============================================================================
// main.cpp — ATmega8 backbone (RF relay) — PURE PACKET FORWARDER
// =============================================================================
//
// ROLE IN THE PARK SYSTEM
// -----------------------
//   Reader nodes  <--nRF24-->  THIS BACKBONE  <--nRF24-->  Gateway  <--USB--> PC
//
// This MCU does NOT validate payments, calculate credits, drive RFID readers,
// or talk to a game machine. It only relays opaque 32-byte frames between two
// RF "zones" (reader pipe vs gateway pipe), preserving every byte verbatim.
//
// LOOP CONTRACT
// -------------
//   1) wdt_reset()     — pet the ATmega8 watchdog (BACKBONE_WDTO).
//   2) drain RX        — handle every queued frame this iteration so the radio
//                        FIFO never backs up under contention.
//   3) router tick     — prune expired pending-swipe entries (housekeeping).
//   4) optional debug  — periodic, low-frequency UART line for operator visibility.
//
// SRAM DISCIPLINE
// ---------------
// One static 32-byte RX scratch buffer (proto::PAYLOAD_MAX). Everything else
// either lives in flash (KNOWN_NODES) or in tiny module-static counters.
//
// FAILURE POLICY
// --------------
// - Radio init fails  -> latch error, retry init periodically (no lockup).
// - TX repeatedly fails -> router invokes backbone_radio_recover(); we keep
//   listening so the system self-heals when RF conditions improve.
// - Backbone never originates PAY_RESP / STATUS_PUSH on behalf of others
//   (per spec: "do NOT process payment logic").
//
// =============================================================================

#include <Arduino.h>

#if defined(__AVR__)
#include <avr/wdt.h>
#endif

#include "backbone_config.h"
#include "backbone_protocol.h"
#include "backbone_radio.h"
#include "backbone_router.h"

namespace {

// Single shared RX scratch buffer; reused for every drained frame.
uint8_t g_rx_frame[proto::PAYLOAD_MAX];

// Set by setup(): when true, loop() periodically retries radio init instead of
// trying to forward (the relay role is meaningless without a working radio).
bool g_radio_alive = false;

// millis() deadline for the next retry of backbone_radio_begin() while down.
uint32_t g_next_radio_retry_ms = 0;
constexpr uint32_t kRadioRetryIntervalMs = 1000UL;

// Optional periodic debug heartbeat (compiled out when BACKBONE_DEBUG_UART=0).
#if BACKBONE_DEBUG_UART
constexpr uint32_t kDebugHeartbeatMs = 5000UL;
uint32_t g_next_debug_ms = 0;
#endif

// Arm watchdog with the platform-appropriate timeout (BACKBONE_WDTO).
void wdt_arm() {
#if defined(__AVR__)
  wdt_disable();
  wdt_enable(BACKBONE_WDTO);
#endif
}

// --- Optional debug helpers (no-ops when BACKBONE_DEBUG_UART == 0) ----------

#if BACKBONE_DEBUG_UART

void dbg_print_banner() {
  Serial.print(F("BB"));
  Serial.print(static_cast<int>(bbcfg::kBackboneId));
  Serial.println(F("_ok"));
}

void dbg_print_radio_down() {
  Serial.println(F("radio_init_failed"));
}

void dbg_print_rx(uint8_t pipe, uint8_t msg_type) {
  Serial.print(F("RX p"));
  Serial.print(static_cast<int>(pipe));
  Serial.print(F(" "));
  Serial.println(proto::msg_type_name(msg_type));
}

void dbg_print_heartbeat() {
  const BackboneStats& s = backbone_router_stats();
  Serial.print(F("HB up="));
  Serial.print(s.uplink_forwarded);
  Serial.print(F(" down="));
  Serial.print(s.downlink_forwarded);
  Serial.print(F(" txfail="));
  Serial.print(s.tx_failures);
  Serial.print(F(" drop="));
  Serial.print(s.pending_dropped);
  Serial.print(F(" orph="));
  Serial.println(s.orphan_pay_resp);
}

#else

inline void dbg_print_banner() {}
inline void dbg_print_radio_down() {}
inline void dbg_print_rx(uint8_t, uint8_t) {}
inline void dbg_print_heartbeat() {}

#endif  // BACKBONE_DEBUG_UART

// Drain every pending RX frame this iteration. Pipe index decides direction.
void drain_radio_rx() {
  uint8_t pipe = 0;
  while (backbone_radio_try_recv(g_rx_frame, &pipe)) {
    dbg_print_rx(pipe, g_rx_frame[0]);

    if (pipe == 1) {
      // Reader-side uplink → forward to gateway (and remember the swipe).
      (void)backbone_router_handle_uplink(g_rx_frame);
    } else if (pipe == 2) {
      // Gateway-side downlink → forward to a specific reader (or fan-out).
      (void)backbone_router_handle_downlink(g_rx_frame);
    }
    // Other pipe indices (0, 3..5) are not opened in begin(); ignore safely.
  }
}

// Periodically retry radio init while the radio is down, keeping WDT happy.
void try_revive_radio(uint32_t now_ms) {
  if (g_radio_alive) return;
  if (static_cast<int32_t>(now_ms - g_next_radio_retry_ms) < 0) return;

  g_next_radio_retry_ms = now_ms + kRadioRetryIntervalMs;
  if (backbone_radio_begin()) {
    g_radio_alive = true;
    backbone_router_begin();
    dbg_print_banner();
  } else {
    dbg_print_radio_down();
  }
}

}  // namespace

void setup() {
#if BACKBONE_DEBUG_UART
  Serial.begin(bbcfg::kSerialBaud);
#endif

  // Arm WDT BEFORE touching the radio so a hung SPI bus cannot brick the MCU.
  wdt_arm();
  wdt_reset();

  backbone_router_begin();

  g_radio_alive = backbone_radio_begin();
  if (g_radio_alive) {
    dbg_print_banner();
  } else {
    dbg_print_radio_down();
    g_next_radio_retry_ms = millis() + kRadioRetryIntervalMs;
  }

#if BACKBONE_DEBUG_UART
  g_next_debug_ms = millis() + kDebugHeartbeatMs;
#endif
}

void loop() {
#if defined(__AVR__)
  wdt_reset();
#endif

  const uint32_t now = millis();

  if (g_radio_alive) {
    drain_radio_rx();
    backbone_router_tick(now);
  } else {
    try_revive_radio(now);
  }

#if BACKBONE_DEBUG_UART
  if (static_cast<int32_t>(now - g_next_debug_ms) >= 0) {
    g_next_debug_ms = now + kDebugHeartbeatMs;
    dbg_print_heartbeat();
  }
#endif
}
