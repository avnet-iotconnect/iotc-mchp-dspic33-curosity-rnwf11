/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <xc.h>
#include <stdio.h>
#include "tmc7300.h"
#include "../bsp/uart3.h"
#include "../bsp/systick.h"
#include "../timer/delay.h"

#define SYNC_BYTE 0x05U
#define WRITE_FLAG 0x80U
#define REPLY_ADDRESS 0xFFU
#define READ_TIMEOUT_MS 5U

// Confirmed on hardware: a datagram sent right after the driver's read reply
// is dropped. Without this idle gap every other back-to-back read failed, and
// the GCONF write that follows the startup read was lost, which left
// PWM_direct off so the motors never ran.
#define BUS_IDLE_US 500U

#define REG_GCONF 0x00U
#define REG_GSTAT 0x01U
#define REG_IFCNT 0x02U
#define REG_SLAVECONF 0x03U
#define REG_IOIN 0x06U
#define REG_CURRENT_LIMIT 0x10U
#define REG_PWM_AB 0x22U
#define REG_CHOPCONF 0x6CU
#define REG_DRV_STATUS 0x6FU
#define REG_PWMCONF 0x70U

#define GSTAT_U3V5 0x04UL // supply below 3.5V, live (not latched)
#define DRV_STATUS_FAULTS 0x3EUL // ot, s2ga, s2gb, s2vsa, s2vsb: driver shut down

// mikroBUS B pins, see the dsPIC33AK512MPS512 General Purpose DIM info sheet (DS70005563)
#define EN_LAT LATGbits.LATG1 // mikroBUS PWM, RG1
#define AD0_LAT LATAbits.LATA3 // mikroBUS AN, RA3
#define AD1_LAT LATBbits.LATB12 // mikroBUS CS, RB12

// CRC8-ATM, LSB first, per TMC7300 datasheet section 4.2.
static uint8_t Crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (((crc >> 7) ^ (byte & 0x01U)) != 0U) {
                crc = (uint8_t)((crc << 1) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1);
            }
            byte >>= 1;
        }
    }
    return crc;
}

// The click joins RX and TX into the TMC7300's single UART pin, so every byte
// sent is also received back. Callers flush or skip that echo.
static void SendDatagram(const uint8_t *data, size_t length)
{
    UART3_Flush();
    for (size_t i = 0; i < length; i++) {
        UART3_Write(data[i]);
    }
    while (!UART3_IsTxDone()) {
    }
}

static void WriteRegister(uint8_t address, uint8_t reg, uint32_t value)
{
    uint8_t datagram[8] = {
        SYNC_BYTE,
        address,
        (uint8_t)(reg | WRITE_FLAG),
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value,
        0
    };
    datagram[7] = Crc8(datagram, 7);
    SendDatagram(datagram, sizeof(datagram));
    DELAY_microseconds(BUS_IDLE_US); // also lets the last echoed byte arrive before the flush
    UART3_Flush();
}

// Skips the echoed request by waiting for the reply's own header: sync byte,
// master address 0xFF, then the register that was asked for.
static bool ReadReply(uint8_t reg, uint32_t *value)
{
    uint8_t reply[8];
    size_t count = 0;
    uint32_t start = SYSTICK_GetMillis();
    while ((uint32_t)(SYSTICK_GetMillis() - start) <= READ_TIMEOUT_MS) {
        if (!UART3_IsRxReady()) {
            continue;
        }
        uint8_t byte = UART3_Read();
        reply[count++] = byte;
        bool is_header_ok =
            (reply[0] == SYNC_BYTE) &&
            ((count < 2U) || (reply[1] == REPLY_ADDRESS)) &&
            ((count < 3U) || (reply[2] == reg));
        if (!is_header_ok) {
            // The mismatching byte may itself start the real reply.
            reply[0] = byte;
            count = (byte == SYNC_BYTE) ? 1U : 0U;
            continue;
        }
        if (count == sizeof(reply)) {
            if (reply[7] != Crc8(reply, 7)) {
                return false;
            }
            *value = ((uint32_t)reply[3] << 24) | ((uint32_t)reply[4] << 16) | ((uint32_t)reply[5] << 8) | reply[6];
            return true;
        }
    }
    return false;
}

static bool ReadRegister(uint8_t address, uint8_t reg, uint32_t *value)
{
    uint8_t request[4] = {SYNC_BYTE, address, reg, 0};
    request[3] = Crc8(request, 3);
    SendDatagram(request, sizeof(request));
    bool is_ok = ReadReply(reg, value);
    DELAY_microseconds(BUS_IDLE_US);
    return is_ok;
}

void TMC7300_Initialize(void)
{
    EN_LAT = 0;
    AD0_LAT = 0;
    AD1_LAT = 0;
    TRISGbits.TRISG1 = 0;
    ANSELAbits.ANSELA3 = 0;
    TRISAbits.TRISA3 = 0;
    ANSELBbits.ANSELB12 = 0;
    TRISBbits.TRISB12 = 0;

    // UART3 TX (RB11) idles high; RX (RG0) is a digital input by default.
    LATBbits.LATB11 = 1;
    ANSELBbits.ANSELB11 = 0;
    TRISBbits.TRISB11 = 0;
    UART3_Initialize();
}

bool TMC7300_Configure(uint8_t address, int current_limit)
{
    uint32_t ioin;
    if (!ReadRegister(address, REG_IOIN, &ioin)) {
        return false;
    }

    // PWM_direct must be set before EN goes high, or the motors start
    // running on their own (datasheet "Attention" note, section 3).
    WriteRegister(address, REG_GCONF, 0x03UL); // PWM_direct, extcap (as Mikroe's own driver sets it)
    WriteRegister(address, REG_SLAVECONF, 0x0200UL); // SENDDELAY=2, needed with several drivers on the bus
    WriteRegister(address, REG_PWM_AB, 0);
    WriteRegister(address, REG_CURRENT_LIMIT, ((uint32_t)(current_limit & 0x1F) << 8) | 0x01UL); // IRUN, motorrun
    WriteRegister(address, REG_CHOPCONF, 0x8001UL); // TBL=%01, enabledrv
    WriteRegister(address, REG_PWMCONF, 0x00110000UL); // freewheel when stopped, fPWM = 2/683 fCLK
    WriteRegister(address, REG_GSTAT, 0x07UL); // clear reset/fault flags

    uint32_t gconf = 0;
    if (!ReadRegister(address, REG_GCONF, &gconf) || ((gconf & 0x01UL) == 0U)) {
        printf("Motor driver %u configuration didn't take (GCONF=%lx)\r\n", address, (unsigned long)gconf);
        return false;
    }
    return true;
}

void TMC7300_SetEnabled(bool is_enabled)
{
    EN_LAT = is_enabled ? 1 : 0;
}

static uint32_t EncodePwm(int pwm)
{
    if (pwm > TMC7300_PWM_MAX) {
        pwm = TMC7300_PWM_MAX;
    } else if (pwm < -TMC7300_PWM_MAX) {
        pwm = -TMC7300_PWM_MAX;
    }
    return (uint32_t)pwm & 0x1FFUL; // 9-bit two's complement
}

void TMC7300_SetPwm(uint8_t address, int pwm_a, int pwm_b)
{
    WriteRegister(address, REG_PWM_AB, EncodePwm(pwm_a) | (EncodePwm(pwm_b) << 16));
}

bool TMC7300_ReadHealth(uint8_t address, bool *is_supply_low, bool *is_faulted)
{
    uint32_t gstat;
    uint32_t drv_status;
    if (!ReadRegister(address, REG_GSTAT, &gstat) || !ReadRegister(address, REG_DRV_STATUS, &drv_status)) {
        return false;
    }
    *is_supply_low = (gstat & GSTAT_U3V5) != 0U;
    *is_faulted = (drv_status & DRV_STATUS_FAULTS) != 0U;
    return true;
}

void TMC7300_FormatStatus(uint8_t address, char *text, size_t size)
{
    static const struct
    {
        const char *name;
        uint8_t reg;
    } regs[] = {
        {"g", REG_GCONF},
        {"s", REG_GSTAT},
        {"i", REG_IFCNT},
        {"d", REG_DRV_STATUS},
    };
    size_t used = 0;
    text[0] = '\0';
    for (size_t i = 0; (i < sizeof(regs) / sizeof(regs[0])) && (used < size); i++) {
        uint32_t value;
        int written;
        if (ReadRegister(address, regs[i].reg, &value)) {
            written = snprintf(text + used, size - used, "%s%s=%lx", (i > 0) ? " " : "", regs[i].name, (unsigned long)value);
        } else {
            written = snprintf(text + used, size - used, "%s%s=?", (i > 0) ? " " : "", regs[i].name);
        }
        if (written < 0) {
            return;
        }
        used += (size_t)written;
    }
}
