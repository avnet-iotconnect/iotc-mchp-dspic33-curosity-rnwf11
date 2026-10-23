/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "provisioning.h"
#include "../mcc_generated_files/uart/uart1.h"

static char line[256];

// Reads one \n or \r\n terminated line (without the terminator) into line.
// An overlong line is discarded and reading continues with the next one.
static size_t ReadLineBlocking(void)
{
    size_t length = 0;
    for (;;) {
        if (!UART1_IsRxReady()) {
            continue;
        }
        char c = (char)UART1_Read();
        if (c == '\n') {
            line[length] = '\0';
            return length;
        }
        if (c == '\r') {
            continue;
        }
        if (length >= (sizeof(line) - 1U)) {
            length = 0;
            continue;
        }
        line[length++] = c;
    }
}

static bool ApplyField(DeviceConfig *cfg, const char *key, const char *value)
{
    static const struct
    {
        const char *key;
        size_t offset;
        size_t size;
    } text_fields[] = {
        {"WIFI_SSID", offsetof(DeviceConfig, wifi_ssid), sizeof(((DeviceConfig *)0)->wifi_ssid)},
        {"WIFI_PASSWORD", offsetof(DeviceConfig, wifi_password), sizeof(((DeviceConfig *)0)->wifi_password)},
        {"IOTC_CPID", offsetof(DeviceConfig, iotc_cpid), sizeof(((DeviceConfig *)0)->iotc_cpid)},
        {"IOTC_ENV", offsetof(DeviceConfig, iotc_env), sizeof(((DeviceConfig *)0)->iotc_env)},
        {"IOTC_DUID", offsetof(DeviceConfig, iotc_duid), sizeof(((DeviceConfig *)0)->iotc_duid)},
        {"MQTT_BROKER_HOST", offsetof(DeviceConfig, mqtt_broker_host), sizeof(((DeviceConfig *)0)->mqtt_broker_host)},
        {"MQTT_USERNAME", offsetof(DeviceConfig, mqtt_username), sizeof(((DeviceConfig *)0)->mqtt_username)},
        {"MQTT_PUB_TOPIC", offsetof(DeviceConfig, mqtt_pub_topic), sizeof(((DeviceConfig *)0)->mqtt_pub_topic)},
        {"MQTT_C2D_TOPIC", offsetof(DeviceConfig, mqtt_c2d_topic), sizeof(((DeviceConfig *)0)->mqtt_c2d_topic)},
        {"MQTT_ACK_TOPIC", offsetof(DeviceConfig, mqtt_ack_topic), sizeof(((DeviceConfig *)0)->mqtt_ack_topic)},
        {"RNWF_CA_NAME", offsetof(DeviceConfig, rnwf_ca_name), sizeof(((DeviceConfig *)0)->rnwf_ca_name)},
        {"RNWF_CERT_NAME", offsetof(DeviceConfig, rnwf_cert_name), sizeof(((DeviceConfig *)0)->rnwf_cert_name)},
        {"RNWF_KEY_NAME", offsetof(DeviceConfig, rnwf_key_name), sizeof(((DeviceConfig *)0)->rnwf_key_name)},
    };

    if (strcmp(key, "MQTT_BROKER_PORT") == 0) {
        cfg->mqtt_broker_port = (uint16_t)atoi(value);
        return true;
    }
    for (size_t i = 0; i < sizeof(text_fields) / sizeof(text_fields[0]); i++) {
        if (strcmp(key, text_fields[i].key) == 0) {
            snprintf((char *)cfg + text_fields[i].offset, text_fields[i].size, "%s", value);
            return true;
        }
    }
    return false;
}

bool PROVISIONING_CheckForRequest(void)
{
    static size_t peek_length = 0;
    static char peek[16]; // just long enough to match "PROVISION"

    while (UART1_IsRxReady()) {
        char c = (char)UART1_Read();
        if (c == '\n') {
            peek[peek_length] = '\0';
            bool is_request = (strcmp(peek, "PROVISION") == 0);
            peek_length = 0;
            if (is_request) {
                return true;
            }
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (peek_length < (sizeof(peek) - 1U)) {
            peek[peek_length++] = c;
        } else {
            peek_length = 0; // not "PROVISION", resync on the next line
        }
    }
    return false;
}

bool PROVISIONING_RunOnce(DeviceConfig *out)
{
    memset(out, 0, sizeof(*out));
    for (;;) {
        if (ReadLineBlocking() == 0) {
            continue;
        }
        if (strcmp(line, "END") == 0) {
            break;
        }
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            printf("ERROR:malformed line\r\n");
            return false;
        }
        *equals = '\0';
        // An unknown key fails loudly rather than leaving a field at its
        // default, so a typo in the script can't go unnoticed.
        if (!ApplyField(out, line, equals + 1)) {
            printf("ERROR:unknown key %s\r\n", line);
            return false;
        }
    }
    if (!DEVICE_CONFIG_Save(out)) {
        printf("ERROR:flash write failed\r\n");
        return false;
    }
    printf("OK\r\n");
    return true;
}
