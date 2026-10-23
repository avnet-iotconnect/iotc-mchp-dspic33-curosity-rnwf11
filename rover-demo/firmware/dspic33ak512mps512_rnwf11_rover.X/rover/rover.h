/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

/* Four-wheel mecanum rover driven by one DC Motor 22 click (TMC7300, see
 * tmc7300.h). Each of its two channels drives one diagonal pair of wheels
 * wired in parallel, which covers forward/backward, strafing and diagonal
 * moves but not rotating in place (that needs the two wheels of a diagonal
 * pair to turn opposite ways).
 *
 * Every move runs for a limited time and then stops by itself, so a lost
 * connection or a missed "stop" can never leave the rover driving.
 */

#ifndef ROVER_H
#define ROVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#define ROVER_DEFAULT_DURATION_MS 1000U
#define ROVER_MAX_DURATION_MS 10000U

typedef enum
{
    ROVER_PAIR_FL_RR, // front-left + rear-right, click channel A (MA1/MA2)
    ROVER_PAIR_FR_RL, // front-right + rear-left, click channel B (MB1/MB2)
    ROVER_PAIR_COUNT
} RoverPair;

// Returns false if the motor driver didn't answer, in which case the rover
// stays stopped.
bool ROVER_Initialize(void);

bool ROVER_IsReady(void);

// Drives for duration_ms, then stops. forward is + forward / - backward and
// right is + strafe right / - strafe left, each -100..100 percent.
void ROVER_Drive(int forward, int right, unsigned int duration_ms);

// Runs each diagonal pair at its own speed (-100..100 percent, + being the
// direction that drives the rover forward) for duration_ms. Useful for
// checking wiring and wheel direction.
void ROVER_SetPairs(const int percent[ROVER_PAIR_COUNT], unsigned int duration_ms);

// Scales each pair to trim_percent (0..100) of what it's commanded, so a
// stronger pair can be slowed to match the other. Starts at 100 for both and
// isn't saved across resets.
void ROVER_SetTrim(const int trim_percent[ROVER_PAIR_COUNT]);

void ROVER_GetTrim(int trim_percent[ROVER_PAIR_COUNT]);

// Stops all wheels immediately, without ramping down.
void ROVER_Stop(void);

// Ramps wheel speeds toward their targets and ends timed moves. Call from
// the main loop at least every few milliseconds.
void ROVER_Task(void);

// Returns false if the motor driver doesn't answer; see TMC7300_ReadHealth()
// for the two flags.
bool ROVER_ReadHealth(bool *is_supply_low, bool *is_faulted);

void ROVER_FormatDriverStatus(char *text, size_t size);

#ifdef __cplusplus
}
#endif

#endif // ROVER_H
