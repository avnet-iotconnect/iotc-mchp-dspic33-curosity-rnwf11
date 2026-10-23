/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

/* Minimal on-chip flash word-program/page-erase driver, written from the
 * "Flash Program Memory" chapter (section 6, Run-Time Self-Programming /
 * NVMCON) of the dsPIC33AK512MPS512 Family Data Sheet (DS70005591). It is
 * not MCC Melody output; MCC Melody also offers a Flash/NVM driver for this
 * family if you'd rather swap in a Microchip-generated one.
 *
 * The config page (NVM_FLASH_CONFIG_PAGE_ADDR) is the last 4KB page of the
 * 512K flash range (0x800000-0x87FFFC). This project doesn't use the
 * dual-partition/bootloader (BOOTSWP) feature, so nothing else claims that
 * page, and the device Configuration Words live in a separate address space
 * (~0x7F3010).
 */

#ifndef NVM_FLASH_H
#define NVM_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NVM_FLASH_CONFIG_PAGE_ADDR 0x87F000UL
#define NVM_FLASH_PAGE_SIZE 4096U
#define NVM_FLASH_WORD_SIZE 16U // 128 bits per flash word, the RTSP program granularity

bool NVM_FLASH_ErasePage(void);

// offset (from NVM_FLASH_CONFIG_PAGE_ADDR) and length must both be multiples
// of NVM_FLASH_WORD_SIZE, and the target must already be erased (all 1s):
// call NVM_FLASH_ErasePage() first when overwriting existing data.
bool NVM_FLASH_WriteBytes(size_t offset, const uint8_t *data, size_t length);

void NVM_FLASH_ReadBytes(size_t offset, uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // NVM_FLASH_H
