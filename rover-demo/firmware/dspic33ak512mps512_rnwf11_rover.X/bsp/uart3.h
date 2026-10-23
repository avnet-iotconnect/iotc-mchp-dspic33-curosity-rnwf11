/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

// Minimal polled UART3 driver for mikroBUS B (TX = RP28/RB11, RX = RP97/RG0,
// mapped via PPS in mcc_generated_files/system/src/pins.c), carrying the DC
// Motor 22 click's TMC7300 single-wire UART.

#ifndef UART3_H
#define UART3_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

void UART3_Initialize(void); // 115200 8-N-1

void UART3_Write(uint8_t data);

bool UART3_IsTxDone(void);

bool UART3_IsRxReady(void);

uint8_t UART3_Read(void); // call only when UART3_IsRxReady()

// Discards the receive FIFO and clears a receive overflow, which would
// otherwise stop reception.
void UART3_Flush(void);

#ifdef __cplusplus
}
#endif

#endif // UART3_H
