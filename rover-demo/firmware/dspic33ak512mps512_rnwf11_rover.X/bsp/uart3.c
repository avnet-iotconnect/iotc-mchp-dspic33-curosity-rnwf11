/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <xc.h>
#include "uart3.h"

void UART3_Initialize(void)
{
    // Same mode as MCC's UART2_Initialize(): asynchronous 8-bit, 1 stop bit,
    // 100 MHz standard-speed peripheral clock, fractional baud (CLKMOD).
    U3CON = 0x8000000UL;
    U3STAT = 0x2E0080UL;
    U3BRG = 868UL; // 100 MHz / 868 = 115207 baud (0.006% error)
    U3CONbits.ON = 1;
    U3CONbits.TXEN = 1;
    U3CONbits.RXEN = 1;
}

void UART3_Write(uint8_t data)
{
    while (U3STATbits.TXBF == 1) {
    }
    U3TXB = data;
}

bool UART3_IsTxDone(void)
{
    return U3STATbits.TXMTIF && U3STATbits.TXBE;
}

bool UART3_IsRxReady(void)
{
    return U3STATbits.RXBE == 0;
}

uint8_t UART3_Read(void)
{
    U3STATbits.RXFOIF = 0;
    return (uint8_t)U3RXB;
}

void UART3_Flush(void)
{
    while (U3STATbits.RXBE == 0) {
        (void)U3RXB;
    }
    U3STATbits.RXFOIF = 0;
}
