/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rover_commands.h"
#include "../rover/rover.h"

#define ARGS_MAX 4

// Ack messages stay at 25 characters or less: the whole AT+MQTTPUB line that
// carries an ack, including a topic that grows with the device's Unique ID,
// has to fit in the RNWF11's 195-byte limit (see app/iotc_rnwf11.c).
#define USAGE_DIRECTION "speed 0-100, ms 1-10000"
#define USAGE_MOVE "move <fwd> <right> [ms]"
#define USAGE_PAIRS "pairs <a> <b> [ms]"
#define USAGE_TRIM "trim values 50-100"

static char last_command[64];

// Splits "name a b c" in place. Returns the number of integer arguments
// parsed into args, or -1 if any argument isn't an integer or there are too
// many.
static int ParseArgs(char *line, const char **name, long args[ARGS_MAX])
{
    *name = strtok(line, " ");
    int count = 0;
    for (char *token = strtok(NULL, " "); token != NULL; token = strtok(NULL, " ")) {
        char *end = NULL;
        long value = strtol(token, &end, 10);
        if ((end == token) || (*end != '\0') || (count >= ARGS_MAX)) {
            return -1;
        }
        args[count++] = value;
    }
    return count;
}

static bool IsInRange(long value, long min, long max)
{
    return (value >= min) && (value <= max);
}

// The optional trailing duration, at args[index] when present.
static bool GetDuration(const long *args, int count, int index, unsigned int *duration_ms)
{
    if (count <= index) {
        *duration_ms = ROVER_DEFAULT_DURATION_MS;
        return true;
    }
    if (!IsInRange(args[index], 1, ROVER_MAX_DURATION_MS)) {
        return false;
    }
    *duration_ms = (unsigned int)args[index];
    return true;
}

static bool RunCommand(const char *name, const long *args, int count, const char **message)
{
    static const struct
    {
        const char *name;
        int forward;
        int right;
    } directions[] = {
        {"forward", 1, 0},
        {"backward", -1, 0},
        {"right", 0, 1},
        {"left", 0, -1},
    };
    unsigned int duration_ms;

    if (strcmp(name, "stop") == 0) {
        ROVER_Stop();
        return true;
    }
    if (strcmp(name, "diag") == 0) {
        static char status[32];
        ROVER_FormatDriverStatus(status, sizeof(status));
        *message = status;
        return true;
    }
    if (strcmp(name, "trim") == 0) {
        if ((count != 2) || !IsInRange(args[0], 50, 100) || !IsInRange(args[1], 50, 100)) {
            *message = USAGE_TRIM;
            return false;
        }
        const int trim[ROVER_PAIR_COUNT] = {(int)args[0], (int)args[1]};
        ROVER_SetTrim(trim);
        return true;
    }
    if (!ROVER_IsReady()) {
        *message = "no motor driver";
        return false;
    }
    for (size_t i = 0; i < sizeof(directions) / sizeof(directions[0]); i++) {
        if (strcmp(name, directions[i].name) != 0) {
            continue;
        }
        if ((count < 1) || (count > 2) || !IsInRange(args[0], 0, 100) || !GetDuration(args, count, 1, &duration_ms)) {
            *message = USAGE_DIRECTION;
            return false;
        }
        int speed = (int)args[0];
        ROVER_Drive(directions[i].forward * speed, directions[i].right * speed, duration_ms);
        return true;
    }
    if (strcmp(name, "move") == 0) {
        if (
            (count < 2) || (count > 3) ||
            !IsInRange(args[0], -100, 100) || !IsInRange(args[1], -100, 100) ||
            !GetDuration(args, count, 2, &duration_ms)
        ) {
            *message = USAGE_MOVE;
            return false;
        }
        ROVER_Drive((int)args[0], (int)args[1], duration_ms);
        return true;
    }
    if (strcmp(name, "pairs") == 0) {
        if (
            (count < 2) || (count > 3) ||
            !IsInRange(args[0], -100, 100) || !IsInRange(args[1], -100, 100) ||
            !GetDuration(args, count, 2, &duration_ms)
        ) {
            *message = USAGE_PAIRS;
            return false;
        }
        const int percent[ROVER_PAIR_COUNT] = {(int)args[0], (int)args[1]};
        ROVER_SetPairs(percent, duration_ms);
        return true;
    }
    if ((strcmp(name, "rotate-left") == 0) || (strcmp(name, "rotate-right") == 0)) {
        *message = "rotation not supported";
        return false;
    }
    *message = "unknown command";
    return false;
}

bool ROVER_COMMANDS_Handle(const char *command_line, const char **message)
{
    char line[64];
    snprintf(line, sizeof(line), "%s", command_line);
    const char *name = NULL;
    long args[ARGS_MAX];
    int count = ParseArgs(line, &name, args);
    if ((name == NULL) || (count < 0)) {
        *message = "args must be integers";
        return false;
    }
    bool is_ok = RunCommand(name, args, count, message);
    if (is_ok) {
        snprintf(last_command, sizeof(last_command), "%s", command_line);
    }
    return is_ok;
}

const char *ROVER_COMMANDS_GetLast(void)
{
    return last_command;
}
