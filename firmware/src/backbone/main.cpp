// =============================================================================
// main.cpp — ATmega8 backbone relay between reader mesh and USB gateway mesh
// =============================================================================
//
// Topology reminder:
//   Reader NODE  --nRF24-->  BACKBONE (this firmware)  --nRF24-->  GATEWAY  --UART-->  PC
//
// This image never parses payment rules — it only moves fixed-width Shockburst payloads
// between RF pipes while keeping the watchdog fed and the radio out of wedged states.
//
// =============================================================================

// millis(), analogRead, randomSeed — timing + entropy for backoff consistency with gateway/node.
#include <Arduino.h>

#if defined(__AVR__)
// Watchdog control — backbone must reset if SPI/radio hangs.
#include <avr/wdt.h>
#endif

#include "backbone_config.h"
#include "backbone_radio.h"
#include "backbone_router.h"
#include "park_rf_protocol.h"

namespace {

// One RX scratch buffer — router forwards same bytes without extra copy.
uint8_t g_rx_frame[proto::PAYLOAD_MAX];

bool g_radio_alive = false;
uint32_t g_next_radio_retry_ms = 0;
constexpr uint32_t kRadioRetryIntervalMs = 1000UL;

#if BACKBONE_DEBUG_UART
constexpr uint32_t kDebugHeartbeatMs = 5000UL;
uint32_t g_next_debug_ms = 0;
#endif

void wdt_arm(void) {
#if defined(__AVR__)
  wdt_disable();
  wdt_enable(BACKBONE_WDTO);
#endif
}

#if BACKBONE_DEBUG_UART

void dbg_print_banner(void) {
  Serial.print(F("BB"));
  Serial.print(static_cast<int>(bbcfg::kBackboneId));
  Serial.println(F("_ok"));
}

void dbg_print_radio_down(void) { Serial.println(F("radio_init_failed")); }

void dbg_print_rx(uint8_t pipe, uint8_t msg_type) {
  Serial.print(F("RX p"));
  Serial.print(static_cast<int>(pipe));
  Serial.print(F(" "));
  Serial.println(proto::msg_type_name(msg_type));
}

void dbg_print_heartbeat(void) {
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

inline void dbg_print_banner(void) {}
inline void dbg_print_radio_down(void) {}
inline void dbg_print_rx(uint8_t, uint8_t) {}
inline void dbg_print_heartbeat(void) {}

#endif

void drain_radio_rx(void) {
  uint8_t pipe = 0;
  while (backbone_radio_try_recv(g_rx_frame, &pipe)) {
    dbg_print_rx(pipe, g_rx_frame[0]);

    if (pipe == 1) {
      // Reader mesh → concentrate toward gateway listening on ADDR_SERVER.
      (void)backbone_router_handle_uplink(g_rx_frame);
    } else if (pipe == 2) {
      // PC-originated commands entering from gateway TX pipe toward ADDR_GW_SERVER.
      (void)backbone_router_handle_downlink(g_rx_frame);
    }
  }
}

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

void setup(void) {
#if BACKBONE_DEBUG_UART
  Serial.begin(bbcfg::kSerialBaud);
#endif

  randomSeed(static_cast<uint32_t>(analogRead(0)) ^ millis());

  wdt_arm();
#if defined(__AVR__)
  wdt_reset();
#endif

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

void loop(void) {
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
