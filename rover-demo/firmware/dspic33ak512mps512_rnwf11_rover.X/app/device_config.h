/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

// WiFi + IoTConnect connection settings, persisted in on-chip flash (see
// bsp/nvm_flash.c) and written by tools/provision_device_config.py/.ps1 over
// the UART1 debug console using a simple line-based protocol (see
// app/provisioning.c).

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_CONFIG_MAGIC 0x49435432UL // "ICT2", adds the C2D command and ack topics to "ICT1"

typedef struct
{
    uint32_t magic; // DEVICE_CONFIG_MAGIC if this page holds valid provisioned data
    uint32_t version;

    char wifi_ssid[33];
    char wifi_password[65];

    char iotc_cpid[17];
    char iotc_env[17];
    char iotc_duid[65]; // IoTConnect's resolved MQTT client ID, see tools/provision_device_config.py

    // Resolved once by tools/provision_device_config.py (IoTConnect DRA
    // discovery+identity) and stored here, rather than the firmware doing
    // live HTTPS discovery itself, see DEVELOPER.md for why.
    char mqtt_broker_host[128];
    uint16_t mqtt_broker_port;
    char mqtt_username[192]; // empty string if unused
    char mqtt_pub_topic[128]; // telemetry
    char mqtt_c2d_topic[128]; // cloud-to-device commands, subscribed to
    char mqtt_ack_topic[128]; // command acknowledgements

    // Filenames of the CA/root, device cert, and device key already uploaded
    // to the RNWF11's own filesystem by tools/provision_rnwf11_cert.py
    // (AT+FS), so this firmware never handles private key material directly.
    char rnwf_ca_name[33];
    char rnwf_cert_name[33];
    char rnwf_key_name[33];

    uint32_t crc32; // over every field above, see device_config.c
} DeviceConfig;

// Returns false if the device hasn't been provisioned yet (or the flash page
// is erased/corrupt), in which case *out must not be trusted.
bool DEVICE_CONFIG_Load(DeviceConfig *out);

// Erases the config flash page and writes *cfg, filling in its magic,
// version and CRC32.
bool DEVICE_CONFIG_Save(DeviceConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_CONFIG_H
