/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <stdio.h>
#include <string.h>
#include "iotc_rnwf11.h"
#include "../mcc_generated_files/uart/uart2.h"
#include "../bsp/systick.h"
#include "../rnwf11/rnwf_interface.h"
#include "../rnwf11/rnwf_wifi_service.h"
#include "../rnwf11/rnwf_net_service.h"
#include "../rnwf11/rnwf_mqtt_service.h"
#include "../Libraries/iotc-c-lib/core/include/iotcl.h"
#include "../Libraries/iotc-c-lib/core/include/iotcl_telemetry.h"

#define WIFI_CONNECT_TIMEOUT_MS 20000UL
#define MQTT_CONNECT_TIMEOUT_MS 15000UL
#define MQTT_SUBSCRIBE_TIMEOUT_MS 5000UL

// The RNWF11 rejects any AT+MQTTPUB line longer than this, including the
// topic and trailing \r\n, with "Invalid Parameter" (measured on real
// hardware, see the iotc-mchp-dspic33ck-lvmcb repo's DEVELOPER.md).
#define MQTT_PUB_LINE_MAX 195U
#define MQTT_PUB_LINE_OVERHEAD 24U // AT+MQTTPUB=0,1,0,"",""\r\n

#define C2D_PAYLOAD_MAX 256U
#define C2D_QUEUE_LENGTH 2U

static volatile bool is_wifi_up = false;
static volatile bool is_wifi_failed = false;
static volatile bool is_mqtt_up = false;
static volatile bool is_mqtt_failed = false;
static volatile bool is_subscribed = false;

// Commands arrive as "+MQTTSUBRX:" events while RNWF_EVENT_Handler() is
// mid-parse (it uses strtok() and a shared buffer), so they're copied out
// here and only acted on once it returns, see IOTC_RNWF11_Poll(). Acking a
// command sends an AT command, which must not happen inside that parse.
static char c2d_queue[C2D_QUEUE_LENGTH][C2D_PAYLOAD_MAX];
static size_t c2d_queue_length[C2D_QUEUE_LENGTH];
static size_t c2d_queue_head;
static size_t c2d_queue_count;

static const DeviceConfig *command_cfg;
static IotcCommandHandler command_handler;

static void QueueC2dMessage(const char *sub_rx_args);

static void OnWifiEvent(RNWF_WIFI_EVENT_t event, uint8_t *msg)
{
    (void)msg;
    switch (event) {
    case RNWF_DHCP_DONE:
        is_wifi_up = true;
        break;
    case RNWF_DISCONNECTED:
    case RNWF_CONNECT_FAILED:
        is_wifi_failed = true;
        break;
    default:
        break;
    }
}

static RNWF_RESULT_t OnMqttEvent(RNWF_MQTT_EVENT_t event, uint8_t *msg)
{
    if (event == RNWF_MQTT_SUBCRIBE_MSG) {
        QueueC2dMessage((const char *)msg);
    } else if (event == RNWF_MQTT_SUBCRIBE_ACK) {
        is_subscribed = true;
    } else if (event == RNWF_MQTT_CONNECTED) {
        is_mqtt_up = true;
    } else if (event == RNWF_MQTT_DISCONNECTED) {
        // Also sent ("+MQTTCONN:0") right after a rejected connect attempt,
        // so it ends a connect wait immediately instead of timing out.
        is_mqtt_up = false;
        is_mqtt_failed = true;
    }
    return RNWF_PASS;
}

bool IOTC_RNWF11_ConnectWifi(const DeviceConfig *cfg)
{
    UART2_Initialize();
    RNWF_IF_Init();
    is_wifi_up = false;
    is_wifi_failed = false;
    RNWF_WIFI_SrvCtrl(RNWF_WIFI_SET_CALLBACK, (void *)&OnWifiEvent);

    RNWF_WIFI_PARAM_t param = {
        .mode = RNWF_WIFI_MODE_STA,
        .ssid = cfg->wifi_ssid,
        .passphrase = cfg->wifi_password,
        .security = RNWF_WPA2_MIXED, // the RNWF11 reference firmware's default; change if your AP needs WPA3
        .autoconnect = 0,
    };
    RNWF_WIFI_SrvCtrl(RNWF_SET_WIFI_PARAMS, &param);
    RNWF_WIFI_SrvCtrl(RNWF_STA_CONNECT, NULL);

    // The driver has no event loop of its own: RNWF_EVENT_Handler() services
    // the async event queue (+WSTA..., +MQTT... lines) between AT commands.
    uint32_t start = SYSTICK_GetMillis();
    while (!is_wifi_up && !is_wifi_failed) {
        RNWF_EVENT_Handler();
        if ((uint32_t)(SYSTICK_GetMillis() - start) > WIFI_CONNECT_TIMEOUT_MS) {
            printf("WiFi connect timed out\r\n");
            return false;
        }
    }
    return is_wifi_up;
}

bool IOTC_RNWF11_ConnectMqtt(const DeviceConfig *cfg)
{
    // DOMAIN_NAME is left unset: its maximum length is shorter than an AWS
    // IoT endpoint hostname ("ERROR:0.4,Invalid Parameter"), and SERVER_NAME
    // alone is enough for the TLS SNI hostname.
    const char *tls_cfg_list[6] = {
        [RNWF_NET_TLS_CA_CERT] = cfg->rnwf_ca_name,
        [RNWF_NET_TLS_CERT_NAME] = cfg->rnwf_cert_name,
        [RNWF_NET_TLS_KEY_NAME] = cfg->rnwf_key_name,
        [RNWF_NET_TLS_KEY_PWD] = NULL,
        [RNWF_NET_TLS_SERVER_NAME] = cfg->mqtt_broker_host,
        [RNWF_NET_TLS_DOMAIN_NAME] = NULL,
    };
    RNWF_NET_SOCK_SrvCtrl(RNWF_NET_TLS_CONFIG_1, tls_cfg_list);

    // Not RNWF_MQTT_SrvCtrl(RNWF_MQTT_CONFIG, ...): with RNWF11_SERVICE
    // defined it also sends Azure-only commands (AT+MQTTC=10,1 and an
    // AT+AZUREC Device Twin model ID). This is just its generic subset.
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_PROTO_VER, 3);
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_TLS_CONF, RNWF_NET_TLS_CONFIG_1);
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_BROKER_URL, cfg->mqtt_broker_host);
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_BROKER_PORT, cfg->mqtt_broker_port);
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_CLIENT_ID, cfg->iotc_duid);
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_USERNAME, cfg->mqtt_username);
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_PASSWORD, "");
    RNWF_CMD_SEND_OK_WAIT(NULL, NULL, RNWF_MQTT_SET_KEEPALIVE, 60);

    is_mqtt_up = false;
    is_mqtt_failed = false;
    RNWF_MQTT_SrvCtrl(RNWF_MQTT_SET_CALLBACK, (void *)&OnMqttEvent);
    RNWF_MQTT_SrvCtrl(RNWF_MQTT_CONNECT, NULL);

    uint32_t start = SYSTICK_GetMillis();
    while (!is_mqtt_up && !is_mqtt_failed) {
        RNWF_EVENT_Handler();
        if ((uint32_t)(SYSTICK_GetMillis() - start) > MQTT_CONNECT_TIMEOUT_MS) {
            printf("MQTT connect timed out\r\n");
            return false;
        }
    }
    if (is_mqtt_failed) {
        printf("MQTT connect rejected by broker\r\n");
    }
    return is_mqtt_up;
}

// AT+MQTTPUB wraps the message in "..." without escaping it, so every '"' in
// the JSON would end that argument early ("ERROR:0.5,Incorrect Number of
// Parameters").
static bool EscapeJsonForAtCommand(const char *json, char *out, size_t out_size)
{
    size_t o = 0;
    for (size_t i = 0; json[i] != '\0'; i++) {
        char c = json[i];
        bool needs_escape = (c == '"') || (c == '\\');
        if ((o + (needs_escape ? 2U : 1U)) >= out_size) {
            return false;
        }
        if (needs_escape) {
            out[o++] = '\\';
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

static bool EscapeForPublish(const char *topic, const char *json, char *escaped, size_t escaped_size)
{
    return EscapeJsonForAtCommand(json, escaped, escaped_size) &&
        ((MQTT_PUB_LINE_OVERHEAD + strlen(topic) + strlen(escaped)) <= MQTT_PUB_LINE_MAX);
}

static bool Publish(const char *topic, const char *json, RNWF_MQTT_QOS_t qos)
{
    char escaped[MQTT_PUB_LINE_MAX];
    if (!EscapeForPublish(topic, json, escaped, sizeof(escaped))) {
        printf("Message too long for the RNWF11 to publish: %s\r\n", json);
        return false;
    }
    RNWF_MQTT_FRAME_t frame = {
        .isNew = NEW_MSG,
        .qos = qos,
        .isRetain = NO_RETAIN,
        .topic = topic,
        .message = escaped,
    };
    return RNWF_MQTT_SrvCtrl(RNWF_MQTT_PUBLISH, &frame) == RNWF_PASS;
}

static int GetDriverState(const IotcRnwf11Telemetry *telemetry)
{
    if (!telemetry->is_driver_found) {
        return 0;
    }
    if (telemetry->is_faulted) {
        return 3;
    }
    return telemetry->is_supply_low ? 2 : 1;
}

bool IOTC_RNWF11_SendTelemetry(const DeviceConfig *cfg, const IotcRnwf11Telemetry *telemetry)
{
    IotclMessageHandle msg = iotcl_telemetry_create();
    if (msg == NULL) {
        printf("iotcl_telemetry_create failed\r\n");
        return false;
    }
    // Short names keep the whole publish under MQTT_PUB_LINE_MAX; see
    // DEVELOPER.md for what each one means.
    char command[23]; // fits the longest valid command, "pairs -100 -100 10000"
    snprintf(command, sizeof(command), "%s", telemetry->last_command);
    iotcl_telemetry_set_number(msg, "drv", GetDriverState(telemetry));
    iotcl_telemetry_set_number(msg, "ta", telemetry->trim_fl_rr);
    iotcl_telemetry_set_number(msg, "tb", telemetry->trim_fr_rl);
    iotcl_telemetry_set_string(msg, "cmd", command);
    iotcl_telemetry_set_number(msg, "up", (double)telemetry->uptime_s);

    char *json = iotcl_telemetry_create_serialized_string(msg, false);
    iotcl_telemetry_destroy(msg);
    if (json == NULL) {
        printf("iotcl_telemetry_create_serialized_string failed\r\n");
        return false;
    }
    bool is_sent = Publish(cfg->mqtt_pub_topic, json, MQTT_QOS1);
    printf("Published: %s\r\n", json);
    iotcl_telemetry_destroy_serialized_string(json);
    return is_sent;
}

// Inverse of EscapeJsonForAtCommand(): the module backslash-escapes quotes in
// the quoted fields of "+MQTTSUBRX:" the same way. Copies src into dst up to
// the first unescaped '"' and returns a pointer just past it, or NULL if src
// ends first. dst is always null-terminated (truncated to fit) and *out_length
// is the full unescaped length, so truncation shows as *out_length >= dst_size.
static const char *UnescapeQuotedString(const char *src, char *dst, size_t dst_size, size_t *out_length)
{
    size_t o = 0;
    for (; *src != '\0'; src++) {
        if (*src == '"') {
            dst[(o < dst_size) ? o : (dst_size - 1U)] = '\0';
            if (out_length != NULL) {
                *out_length = o;
            }
            return src + 1;
        }
        if ((*src == '\\') && (src[1] != '\0')) {
            src++;
        }
        if (o < (dst_size - 1U)) {
            dst[o] = *src;
        }
        o++;
    }
    return NULL;
}

// sub_rx_args is everything after "+MQTTSUBRX:", observed on real hardware as
// <DUP>,<QOS>,<RETAIN>,"<TOPIC>","<PAYLOAD>"
static void QueueC2dMessage(const char *sub_rx_args)
{
    const char *p = sub_rx_args;
    for (int skip = 0; (skip < 3) && (p != NULL); skip++) {
        p = strchr(p, ',');
        if (p != NULL) {
            p++;
        }
    }
    if ((p == NULL) || (*p != '"')) {
        printf("Unrecognized MQTT message: %s\r\n", sub_rx_args);
        return;
    }
    char topic[128];
    const char *after_topic = UnescapeQuotedString(p + 1, topic, sizeof(topic), NULL);
    if ((after_topic == NULL) || (after_topic[0] != ',') || (after_topic[1] != '"')) {
        printf("Unrecognized MQTT message: %s\r\n", sub_rx_args);
        return;
    }
    if (c2d_queue_count >= C2D_QUEUE_LENGTH) {
        printf("Command queue full, dropping a command\r\n");
        return;
    }
    size_t slot = (c2d_queue_head + c2d_queue_count) % C2D_QUEUE_LENGTH;
    size_t length = 0;
    if ((UnescapeQuotedString(after_topic + 2, c2d_queue[slot], C2D_PAYLOAD_MAX, &length) == NULL) || (length >= C2D_PAYLOAD_MAX)) {
        printf("Command payload too long or truncated, dropping it\r\n");
        return;
    }
    c2d_queue_length[slot] = length;
    c2d_queue_count++;
}

bool IOTC_RNWF11_SubscribeCommands(const DeviceConfig *cfg, IotcCommandHandler handler)
{
    command_cfg = cfg;
    command_handler = handler;
    c2d_queue_head = 0;
    c2d_queue_count = 0;
    if (cfg->mqtt_c2d_topic[0] == '\0') {
        printf("No command topic provisioned - re-run tools/provision_device_config.py\r\n");
        return false;
    }
    printf("Subscribing to commands on \"%s\"\r\n", cfg->mqtt_c2d_topic);
    is_subscribed = false;
    if (RNWF_MQTT_SrvCtrl(RNWF_MQTT_SUBSCRIBE_QOS1, (void *)cfg->mqtt_c2d_topic) != RNWF_PASS) {
        return false;
    }
    // Confirmed on hardware: the module rejects any publish
    // ("ERROR:8.0,MQTT Error") until the broker acknowledges the
    // subscription with "+MQTTSUB:".
    uint32_t start = SYSTICK_GetMillis();
    while (!is_subscribed) {
        RNWF_EVENT_Handler();
        if ((uint32_t)(SYSTICK_GetMillis() - start) > MQTT_SUBSCRIBE_TIMEOUT_MS) {
            printf("Command subscribe not acknowledged\r\n");
            return false;
        }
    }
    return true;
}

static void SendCommandAck(const char *ack_id, int status, const char *message)
{
    if ((command_cfg == NULL) || (command_cfg->mqtt_ack_topic[0] == '\0')) {
        return;
    }
    char *json = iotcl_c2d_create_cmd_ack_json(ack_id, status, message);
    char escaped[MQTT_PUB_LINE_MAX];
    if ((json != NULL) && (message != NULL) && !EscapeForPublish(command_cfg->mqtt_ack_topic, json, escaped, sizeof(escaped))) {
        // A long Unique ID leaves less room for the message; the status
        // matters more, so send the ack without it.
        iotcl_c2d_destroy_ack_json(json);
        json = iotcl_c2d_create_cmd_ack_json(ack_id, status, NULL);
    }
    if (json == NULL) {
        printf("iotcl_c2d_create_cmd_ack_json failed\r\n");
        return;
    }
    // QoS 0 so there's no +MQTTPUBACK to wait for before the next publish.
    if (!Publish(command_cfg->mqtt_ack_topic, json, MQTT_QOS0)) {
        printf("Command ack not sent\r\n");
    }
    iotcl_c2d_destroy_ack_json(json);
}

void IOTC_RNWF11_OnCommand(IotclC2dEventData data)
{
    // iotcl_c2d_get_command() returns a pointer into the parsed JSON tree,
    // which iotc-c-lib frees right after this callback, not a copy to free.
    const char *raw_command = iotcl_c2d_get_command(data);
    if (raw_command == NULL) {
        return;
    }
    char command[64];
    snprintf(command, sizeof(command), "%s", raw_command);

    const char *message = NULL;
    bool is_ok = (command_handler != NULL) && command_handler(command, &message);
    if (!is_ok && (message == NULL)) {
        message = "invalid command";
    }
    printf("Command \"%s\" -> %s%s%s\r\n", command, is_ok ? "ok" : "failed",
        (message != NULL) ? ": " : "", (message != NULL) ? message : ""
    );

    const char *ack_id = iotcl_c2d_get_ack_id(data);
    if (ack_id != NULL) {
        SendCommandAck(ack_id, is_ok ? IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK : IOTCL_C2D_EVT_CMD_FAILED, message);
    }
}

void IOTC_RNWF11_Poll(void)
{
    RNWF_EVENT_Handler();
    while (c2d_queue_count > 0U) {
        size_t slot = c2d_queue_head;
        c2d_queue_head = (c2d_queue_head + 1U) % C2D_QUEUE_LENGTH;
        c2d_queue_count--;
        iotcl_c2d_process_event_with_length((const uint8_t *)c2d_queue[slot], c2d_queue_length[slot]);
    }
}
