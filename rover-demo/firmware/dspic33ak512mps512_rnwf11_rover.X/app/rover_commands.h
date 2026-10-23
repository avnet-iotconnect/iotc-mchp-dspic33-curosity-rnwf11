/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

/* The rover's IoTConnect commands (see rover/rover.h). Speeds are percent of
 * full speed; the optional trailing time is in milliseconds (default
 * ROVER_DEFAULT_DURATION_MS, at most ROVER_MAX_DURATION_MS), after which the
 * rover stops by itself:
 *   forward <speed> [ms]          backward <speed> [ms]
 *   left <speed> [ms]             right <speed> [ms]          (strafe)
 *   move <forward> <right> [ms]                              (each -100..100)
 *   pairs <fl+rr> <fr+rl> [ms]                               (each -100..100)
 *   trim <fl+rr %> <fr+rl %>                                 (each 50..100, until reset)
 *   stop
 *   diag                          (motor driver status registers, returned in the ack)
 */

#ifndef ROVER_COMMANDS_H
#define ROVER_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Matches IotcCommandHandler (app/iotc_rnwf11.h). *message is left NULL or
// set to static text, so it stays valid after the call.
bool ROVER_COMMANDS_Handle(const char *command_line, const char **message);

// The last command line that was accepted, or "" if none yet.
const char *ROVER_COMMANDS_GetLast(void);

#ifdef __cplusplus
}
#endif

#endif // ROVER_COMMANDS_H
