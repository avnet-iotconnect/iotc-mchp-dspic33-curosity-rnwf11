/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

// 1 ms system tick from TMR1, which MCC Melody already configures for a 1 ms
// period (mcc_generated_files/timer/src/tmr1.c, PR1 = 99999 at 100 MHz).

#ifndef SYSTICK_H
#define SYSTICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void SYSTICK_Initialize(void);

// Wraps after ~49 days, so compare as (uint32_t)(now - start) >= interval.
uint32_t SYSTICK_GetMillis(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTICK_H
