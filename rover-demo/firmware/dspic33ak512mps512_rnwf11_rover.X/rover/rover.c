/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <stdio.h>
#include "rover.h"
#include "tmc7300.h"
#include "../bsp/systick.h"

#define DRIVER_ADDRESS 0U // DC Motor 22 click plugged in unmodified, see tmc7300.h

// Caps duty cycle so the 3-6V TT motors never see more than 6V on average:
// 100% on a 5V motor supply, 6V / supply voltage for anything higher (e.g.
// 70 for a 2S Li-ion pack at up to 8.4V).
#define MAX_DUTY_PERCENT 100

// TMC7300 IRUN (0..31), the current limit per channel, i.e. per pair of
// motors. With the click's 0.1 Ohm sense resistors the limit is
// (IRUN + 1) / 32 * 0.325V / 0.13 Ohm (TMC7300 datasheet 7.1), so 20 gives
// about 1.6A per channel (0.8A per motor), 3.3A worst case all stalled.
#define CURRENT_LIMIT 20

// Duty cycle changes by at most RAMP_STEP every UPDATE_PERIOD_MS (full speed
// in about 250 ms), to soften the motors' start-up current.
#define UPDATE_PERIOD_MS 10U
#define RAMP_STEP 10

// Set a pair's flag if it spins backward on "pairs 30 0" / "pairs 0 30"
// instead of swapping both of its motors' leads. If only one wheel of a pair
// spins backward, swap that motor's leads.
static const bool is_pair_reversed[ROVER_PAIR_COUNT] = {
    [ROVER_PAIR_FL_RR] = false,
    [ROVER_PAIR_FR_RL] = true,
};

static int trim_percent[ROVER_PAIR_COUNT] = {100, 100};

static bool is_ready;
static int target_duty[ROVER_PAIR_COUNT]; // TMC7300 units, -255..255
static int actual_duty[ROVER_PAIR_COUNT];
static bool is_moving;
static uint32_t move_start_ms;
static unsigned int move_duration_ms;
static uint32_t last_update_ms;

static int Clamp(int value, int min, int max)
{
    return (value < min) ? min : ((value > max) ? max : value);
}

static void WriteDriver(void)
{
    int pwm[ROVER_PAIR_COUNT];
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        pwm[p] = is_pair_reversed[p] ? -actual_duty[p] : actual_duty[p];
    }
    TMC7300_SetPwm(DRIVER_ADDRESS, pwm[ROVER_PAIR_FL_RR], pwm[ROVER_PAIR_FR_RL]);
}

bool ROVER_Initialize(void)
{
    TMC7300_Initialize();
    is_ready = TMC7300_Configure(DRIVER_ADDRESS, CURRENT_LIMIT);
    if (is_ready) {
        printf("Motor driver found\r\n");
    } else {
        printf("Motor driver not responding - check the DC Motor 22 click on mikroBUS B\r\n");
    }
    ROVER_Stop();
    return is_ready;
}

bool ROVER_IsReady(void)
{
    return is_ready;
}

void ROVER_SetPairs(const int percent[ROVER_PAIR_COUNT], unsigned int duration_ms)
{
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        int duty_percent = Clamp(percent[p], -100, 100) * MAX_DUTY_PERCENT / 100 * trim_percent[p] / 100;
        target_duty[p] = duty_percent * TMC7300_PWM_MAX / 100;
    }
    move_start_ms = SYSTICK_GetMillis();
    move_duration_ms = (duration_ms > ROVER_MAX_DURATION_MS) ? ROVER_MAX_DURATION_MS : duration_ms;
    is_moving = true;
    TMC7300_SetEnabled(is_ready);
}

void ROVER_Drive(int forward, int right, unsigned int duration_ms)
{
    // Mecanum mixing (rollers forming an X seen from above) without the
    // rotation term: front-left = rear-right = forward + right, and
    // front-right = rear-left = forward - right. Both pairs are scaled down
    // together if either exceeds 100%, keeping the direction of travel.
    int pair[ROVER_PAIR_COUNT] = {
        [ROVER_PAIR_FL_RR] = forward + right,
        [ROVER_PAIR_FR_RL] = forward - right,
    };
    int largest = 100;
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        int magnitude = (pair[p] < 0) ? -pair[p] : pair[p];
        if (magnitude > largest) {
            largest = magnitude;
        }
    }
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        pair[p] = pair[p] * 100 / largest;
    }
    ROVER_SetPairs(pair, duration_ms);
}

void ROVER_SetTrim(const int trim[ROVER_PAIR_COUNT])
{
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        trim_percent[p] = Clamp(trim[p], 0, 100);
    }
}

void ROVER_GetTrim(int trim[ROVER_PAIR_COUNT])
{
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        trim[p] = trim_percent[p];
    }
}

void ROVER_Stop(void)
{
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        target_duty[p] = 0;
        actual_duty[p] = 0;
    }
    is_moving = false;
    TMC7300_SetEnabled(false);
    if (is_ready) {
        WriteDriver();
    }
}

void ROVER_Task(void)
{
    uint32_t now = SYSTICK_GetMillis();
    if ((uint32_t)(now - last_update_ms) < UPDATE_PERIOD_MS) {
        return;
    }
    last_update_ms = now;

    if (is_moving && ((uint32_t)(now - move_start_ms) >= move_duration_ms)) {
        is_moving = false;
        for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
            target_duty[p] = 0;
        }
    }

    bool is_changed = false;
    bool is_any_running = false;
    for (int p = 0; p < ROVER_PAIR_COUNT; p++) {
        int step = Clamp(target_duty[p] - actual_duty[p], -RAMP_STEP, RAMP_STEP);
        if (step != 0) {
            actual_duty[p] += step;
            is_changed = true;
        }
        if (actual_duty[p] != 0) {
            is_any_running = true;
        }
    }
    if (is_changed && is_ready) {
        WriteDriver();
    }
    if (!is_any_running && !is_moving) {
        TMC7300_SetEnabled(false);
    }
}

bool ROVER_ReadHealth(bool *is_supply_low, bool *is_faulted)
{
    return TMC7300_ReadHealth(DRIVER_ADDRESS, is_supply_low, is_faulted);
}

void ROVER_FormatDriverStatus(char *text, size_t size)
{
    TMC7300_FormatStatus(DRIVER_ADDRESS, text, size);
}
