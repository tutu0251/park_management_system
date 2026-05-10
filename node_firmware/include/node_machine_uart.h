#pragma once
// =============================================================================
// node_machine_uart.h — human-readable UART bridge to the game machine firmware
// =============================================================================
//
// begin(): starts hardware USART at NODE_MACHINE_UART_BAUD.
//
// poll(st): non-blocking command parser — writes new MachineStatus into *st when
// keywords recognized; does NOT automatically echo UART status lines (main.cpp
// owns transition side-effects so RF + UART stay coherent).
//
// notify_*(): one-shot outbound messages for billing / fault surfacing.
//
// =============================================================================

#include <stdint.h>

#include "node_protocol.h"

void machine_uart_begin();

void machine_uart_poll(proto::MachineStatus* st);

void machine_uart_game_start();
void machine_uart_pay_fail();
void machine_uart_system_error();

void machine_uart_status_online();
void machine_uart_status_offline();
void machine_uart_status_maintenance();
void machine_uart_status_error();
