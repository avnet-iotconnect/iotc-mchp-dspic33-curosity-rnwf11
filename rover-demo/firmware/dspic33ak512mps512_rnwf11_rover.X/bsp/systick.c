/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include "systick.h"
#include "../mcc_generated_files/timer/tmr1.h"

static volatile uint32_t millis = 0;

static void OnTick(void)
{
    millis++;
}

void SYSTICK_Initialize(void)
{
    TMR1_Initialize();
    TMR1_TimeoutCallbackRegister(&OnTick);
    TMR1_Start();
}

uint32_t SYSTICK_GetMillis(void)
{
    // Keep the TMR1 interrupt from updating the counter mid-read.
    IEC1bits.T1IE = 0;
    uint32_t now = millis;
    IEC1bits.T1IE = 1;
    return now;
}
