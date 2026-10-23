/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

// Glue between the ported RNWF11 AT-command driver (rnwf11/) and IoTConnect:
// WiFi connect, TLS+MQTT configuration/connect, publishing rover telemetry
// via iotc-c-lib's JSON builder, and receiving and acking cloud commands.

#ifndef IOTC_RNWF11_H
#define IOTC_RNWF11_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "device_config.h"
#include "../Libraries/iotc-c-lib/core/include/iotcl_c2d.h"

typedef struct
{
    bool is_driver_found;
    bool is_supply_low;
    bool is_faulted;
    int trim_fl_rr;
    int trim_fr_rl;
    const char *last_command; // "" if none yet
    unsigned long uptime_s;
} IotcRnwf11Telemetry;

// Handles one IoTConnect command, given as the full command line the cloud
// sent ("<name> [args...]"). Returns true if the command was accepted; may
// set *message to static text to return in the command acknowledgement.
typedef bool (*IotcCommandHandler)(const char *command_line, const char **message);

// Brings up UART2 and the RNWF11 AT-command interface and connects to WiFi.
// Blocks until connected, failed, or timed out.
bool IOTC_RNWF11_ConnectWifi(const DeviceConfig *cfg);

// Points the RNWF11's TLS config at the cert/key already on its filesystem
// (tools/provision_rnwf11_cert.py), configures the MQTT client with cfg's
// broker and credentials, and connects. Blocks until connected, failed, or
// timed out.
bool IOTC_RNWF11_ConnectMqtt(const DeviceConfig *cfg);

// Subscribes to cfg->mqtt_c2d_topic and routes each command received on it
// to handler (through iotc-c-lib, whose command callback must be set to
// IOTC_RNWF11_OnCommand), acking it on cfg->mqtt_ack_topic. Call after
// IOTC_RNWF11_ConnectMqtt() succeeds. cfg must stay valid while connected.
bool IOTC_RNWF11_SubscribeCommands(const DeviceConfig *cfg, IotcCommandHandler handler);

void IOTC_RNWF11_OnCommand(IotclC2dEventData data);

bool IOTC_RNWF11_SendTelemetry(const DeviceConfig *cfg, const IotcRnwf11Telemetry *telemetry);

// Services the RNWF11 driver's async events (link up/down, MQTT connect/
// disconnect, ...), then dispatches any commands received since the last
// call. Call from the main loop; it only blocks while acking a command.
void IOTC_RNWF11_Poll(void);

#ifdef __cplusplus
}
#endif

#endif // IOTC_RNWF11_H
