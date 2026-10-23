/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <string.h>
#include "device_config.h"
#include "../bsp/nvm_flash.h"

#define DEVICE_CONFIG_VERSION 1U

// Bitwise CRC-32 (polynomial 0xEDB88320) without a lookup table, to keep
// flash usage small; it only runs at boot and when provisioning.
static uint32_t Crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

bool DEVICE_CONFIG_Load(DeviceConfig *out)
{
    NVM_FLASH_ReadBytes(0, (uint8_t *)out, sizeof(DeviceConfig));
    if (out->magic != DEVICE_CONFIG_MAGIC) {
        return false;
    }
    uint32_t stored_crc = out->crc32;
    out->crc32 = 0;
    uint32_t computed_crc = Crc32((const uint8_t *)out, sizeof(DeviceConfig));
    out->crc32 = stored_crc;
    return stored_crc == computed_crc;
}

bool DEVICE_CONFIG_Save(DeviceConfig *cfg)
{
    cfg->magic = DEVICE_CONFIG_MAGIC;
    cfg->version = DEVICE_CONFIG_VERSION;
    cfg->crc32 = 0;
    cfg->crc32 = Crc32((const uint8_t *)cfg, sizeof(DeviceConfig));

    if (!NVM_FLASH_ErasePage()) {
        return false;
    }

    // NVM_FLASH_WriteBytes() needs a multiple of the 16-byte flash word, so
    // pad a copy rather than require DeviceConfig itself to be sized to one.
    uint8_t padded[((sizeof(DeviceConfig) + NVM_FLASH_WORD_SIZE - 1U) / NVM_FLASH_WORD_SIZE) * NVM_FLASH_WORD_SIZE];
    memset(padded, 0xFF, sizeof(padded));
    memcpy(padded, cfg, sizeof(DeviceConfig));
    return NVM_FLASH_WriteBytes(0, padded, sizeof(padded));
}
