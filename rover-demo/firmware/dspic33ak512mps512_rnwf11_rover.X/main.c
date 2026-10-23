/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

/* dsPIC33AK512MPS512 + RNWF11 mecanum rover: connects to WiFi and an
 * IoTConnect MQTT broker through the RNWF11's own AT-command interface (no
 * MQTT/TLS stack runs on this MCU; the module owns the connection, using a
 * cert/key stored on its own filesystem), drives the rover from IoTConnect
 * commands (app/rover_commands.h) and publishes rover telemetry every
 * TELEMETRY_PERIOD_MS.
 *
 * See README.md for the one-time provisioning steps
 * (tools/provision_rnwf11_cert.py, then tools/provision_device_config.py)
 * this firmware waits for on first boot.
 */

#include <stdio.h>
#include "mcc_generated_files/system/system.h"
#include "bsp/systick.h"
#include "app/device_config.h"
#include "app/provisioning.h"
#include "app/iotc_rnwf11.h"
#include "app/rover_commands.h"
#include "rover/rover.h"
#include "Libraries/iotc-c-lib/core/include/iotcl.h"

#define TELEMETRY_PERIOD_MS 10000UL
#define RECONNECT_DELAY_MS 5000UL
#define WAITING_REPRINT_MS 5000UL
#define PUBLISH_FAILURES_BEFORE_RECONNECT 3

static void PrintWaitingBanner(void)
{
    printf("\r\nNot provisioned yet.\r\n");
    printf("On your PC: run tools/provision_rnwf11_cert.py once (RNWF11 connected\r\n");
    printf("directly to USB), then tools/provision_device_config.py against this\r\n");
    printf("board's console port. Waiting...\r\n");
}

static void WaitForProvisioning(DeviceConfig *cfg)
{
    // Re-printed periodically so a terminal opened late can still tell the
    // board is alive and waiting rather than hung.
    PrintWaitingBanner();
    uint32_t last_print_ms = SYSTICK_GetMillis();
    for (;;) {
        if (PROVISIONING_CheckForRequest()) {
            if (PROVISIONING_RunOnce(cfg)) {
                return;
            }
            printf("Provisioning failed - waiting for retry...\r\n");
            last_print_ms = SYSTICK_GetMillis();
        }
        if ((uint32_t)(SYSTICK_GetMillis() - last_print_ms) >= WAITING_REPRINT_MS) {
            PrintWaitingBanner();
            last_print_ms = SYSTICK_GetMillis();
        }
    }
}

// Waits delay_ms while still servicing provisioning requests, so a device
// stuck retrying a bad WiFi/MQTT config can still be re-provisioned. Only
// writes *new_cfg (and returns true) when re-provisioning succeeds, so a
// failed attempt never clobbers the current config.
static bool DelayCheckingForProvisioning(uint32_t delay_ms, DeviceConfig *new_cfg)
{
    uint32_t start = SYSTICK_GetMillis();
    while ((uint32_t)(SYSTICK_GetMillis() - start) < delay_ms) {
        if (PROVISIONING_CheckForRequest()) {
            if (PROVISIONING_RunOnce(new_cfg)) {
                return true;
            }
            printf("Provisioning failed - waiting for retry...\r\n");
        }
    }
    return false;
}

static void ReadTelemetry(IotcRnwf11Telemetry *telemetry)
{
    telemetry->is_supply_low = false;
    telemetry->is_faulted = false;
    telemetry->is_driver_found = ROVER_IsReady() && ROVER_ReadHealth(&telemetry->is_supply_low, &telemetry->is_faulted);
    int trim[ROVER_PAIR_COUNT];
    ROVER_GetTrim(trim);
    telemetry->trim_fl_rr = trim[ROVER_PAIR_FL_RR];
    telemetry->trim_fr_rl = trim[ROVER_PAIR_FR_RL];
    telemetry->last_command = ROVER_COMMANDS_GetLast();
    telemetry->uptime_s = SYSTICK_GetMillis() / 1000UL;
}

// Runs until the connection is lost or the device is re-provisioned.
static void RunConnected(DeviceConfig *cfg)
{
    IotclClientConfig iotcl_cfg;
    iotcl_init_client_config(&iotcl_cfg);
    iotcl_cfg.device.instance_type = IOTCL_DCT_CUSTOM; // broker/topics are resolved at provisioning time, see device_config.h
    iotcl_cfg.events.cmd_cb = IOTC_RNWF11_OnCommand;
    iotcl_init(&iotcl_cfg);

    if (!IOTC_RNWF11_SubscribeCommands(cfg, ROVER_COMMANDS_Handle)) {
        printf("Command subscribe failed - telemetry only\r\n");
    }

    // A single failed publish (e.g. one that lands while an ack is still in
    // flight) isn't worth a full ~20s WiFi/MQTT reconnect.
    uint32_t last_publish_ms = SYSTICK_GetMillis() - TELEMETRY_PERIOD_MS;
    int publish_failures = 0;
    for (;;) {
        IOTC_RNWF11_Poll();
        ROVER_Task();

        if ((uint32_t)(SYSTICK_GetMillis() - last_publish_ms) >= TELEMETRY_PERIOD_MS) {
            last_publish_ms = SYSTICK_GetMillis();
            IotcRnwf11Telemetry telemetry;
            ReadTelemetry(&telemetry);
            publish_failures = IOTC_RNWF11_SendTelemetry(cfg, &telemetry) ? 0 : (publish_failures + 1);
            if (publish_failures >= PUBLISH_FAILURES_BEFORE_RECONNECT) {
                printf("Publishing keeps failing - reconnecting\r\n");
                return;
            }
        }

        if (PROVISIONING_CheckForRequest()) {
            DeviceConfig new_cfg;
            if (PROVISIONING_RunOnce(&new_cfg)) {
                *cfg = new_cfg;
                return; // reconnect with the freshly provisioned settings
            }
        }
    }
}

int main(void)
{
    SYSTEM_Initialize();
    SYSTICK_Initialize();
    printf("\r\n\r\n=== dsPIC33AK512MPS512 + RNWF11 IoTConnect Mecanum Rover Demo ===\r\n");
    ROVER_Initialize();

    DeviceConfig cfg;
    if (!DEVICE_CONFIG_Load(&cfg)) {
        WaitForProvisioning(&cfg);
    } else {
        printf("Loaded provisioned config for DUID \"%s\"\r\n", cfg.iotc_duid);
    }

    for (;;) {
        DeviceConfig new_cfg;
        printf("Connecting to WiFi SSID \"%s\"...\r\n", cfg.wifi_ssid);
        if (!IOTC_RNWF11_ConnectWifi(&cfg)) {
            printf("WiFi connect failed, retrying in %lums\r\n", RECONNECT_DELAY_MS);
            if (DelayCheckingForProvisioning(RECONNECT_DELAY_MS, &new_cfg)) {
                cfg = new_cfg;
            }
            continue;
        }
        printf("WiFi connected. Connecting to IoTConnect MQTT broker \"%s\"...\r\n", cfg.mqtt_broker_host);
        if (!IOTC_RNWF11_ConnectMqtt(&cfg)) {
            printf("MQTT connect failed, retrying in %lums\r\n", RECONNECT_DELAY_MS);
            if (DelayCheckingForProvisioning(RECONNECT_DELAY_MS, &new_cfg)) {
                cfg = new_cfg;
            }
            continue;
        }
        printf("Connected. Sending telemetry every %lums.\r\n", TELEMETRY_PERIOD_MS);
        RunConnected(&cfg);
        // Reconnecting blocks the main loop, and with it ROVER_Task()'s
        // auto-stop, so the rover must not be left moving.
        ROVER_Stop();
    }
}
