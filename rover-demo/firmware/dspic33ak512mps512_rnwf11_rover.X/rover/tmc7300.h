/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

/* Driver for the TMC7300 dual brushed-DC motor driver on Mikroe's DC Motor 22
 * click, over its single-wire UART (TMC7300 datasheet section 4), on
 * mikroBUS B.
 *
 * Each click answers on a UART address it reads from its AD0 (mikroBUS AN)
 * and AD1 (mikroBUS CS) pins. Both have 10k pull-ups on the click and this
 * firmware drives them low, so a click plugged in unmodified answers on
 * address 0. Several clicks can share one bus (e.g. via a Shuttle click)
 * only if each has a different address, e.g. one with its AN pin left
 * unconnected answers on address 1.
 *
 * The mikroBUS PWM pin is EN on every click, so all drivers on the bus are
 * enabled and disabled together.
 */

#ifndef TMC7300_H
#define TMC7300_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TMC7300_PWM_MAX 255 // PWM_A/PWM_B range is -255..+255 (-100%..+100%)

// Sets up UART3 and the EN/AD0/AD1 pins, with EN low (driver outputs off).
void TMC7300_Initialize(void);

// Configures the driver at address for register-controlled (PWM_direct) DC
// motor operation with both motors stopped. Call while EN is low.
// current_limit is IRUN, 0..31 (1/32..32/32 of the click's sense-resistor
// limit). Returns false if the driver didn't answer or the configuration
// didn't read back correctly.
bool TMC7300_Configure(uint8_t address, int current_limit);

void TMC7300_SetEnabled(bool is_enabled);

// Sets both motors' duty cycles, each clamped to +/-TMC7300_PWM_MAX.
// Write-only, so it's cheap enough to call every few milliseconds.
void TMC7300_SetPwm(uint8_t address, int pwm_a, int pwm_b);

// Reads the driver's live status. is_supply_low means its motor supply (VS)
// is under 3.5V; is_faulted means it shut down on a short or overtemperature.
// Returns false if the driver didn't answer.
bool TMC7300_ReadHealth(uint8_t address, bool *is_supply_low, bool *is_faulted);

// Writes GCONF, GSTAT, IFCNT and DRV_STATUS into text as "g=hex s=hex i=hex
// d=hex" ("?" for a failed read), for troubleshooting. The single-letter
// names keep it short enough to fit in a command acknowledgement.
void TMC7300_FormatStatus(uint8_t address, char *text, size_t size);

#ifdef __cplusplus
}
#endif

#endif // TMC7300_H
