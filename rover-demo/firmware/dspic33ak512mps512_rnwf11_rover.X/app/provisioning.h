/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

/* Line-based provisioning protocol over UART1 (the debug console), driven by
 * tools/provision_device_config.py/.ps1:
 *   - the PC sends "PROVISION", then one "KEY=VALUE" line per DeviceConfig
 *     field (see ApplyField() in provisioning.c for the key names), then "END"
 *   - the device replies "OK" once the config is saved to flash, or
 *     "ERROR:<reason>" if a line couldn't be parsed or the save failed
 * Lines end in \r\n and are matched case-sensitively.
 */

#ifndef PROVISIONING_H
#define PROVISIONING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "device_config.h"

// Non-blocking: only reads bytes already waiting on UART1. Returns true once
// the "PROVISION" line has arrived; the caller should then call
// PROVISIONING_RunOnce() to consume the rest of the exchange.
bool PROVISIONING_CheckForRequest(void);

// Blocks until "END", then saves *out to flash. Returns true if a valid
// config was received and saved.
bool PROVISIONING_RunOnce(DeviceConfig *out);

#ifdef __cplusplus
}
#endif

#endif // PROVISIONING_H
