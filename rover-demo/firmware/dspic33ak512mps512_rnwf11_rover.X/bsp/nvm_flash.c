/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Avnet
 * Authors: Zackary Andraka <zackary.andraka@avnet.com> et al.
 */

#include <xc.h>
#include <string.h>
#include "nvm_flash.h"

// NVMCON.NVMOP values (DS70005591 section 6, "Flash Programming Operations").
#define NVMOP_WORD_PROGRAM 0x1U
#define NVMOP_PAGE_ERASE 0x3U

static bool WaitForCompletion(void)
{
    // The core stalls during the program/erase pulse itself, but WR is still
    // polled since this runs right after setting it.
    while (NVMCONbits.WR == 1) {
    }
    bool is_ok = (NVMCONbits.WRERR == 0);
    NVMCONbits.WREN = 0;
    return is_ok;
}

bool NVM_FLASH_ErasePage(void)
{
    NVMADR = NVM_FLASH_CONFIG_PAGE_ADDR;
    NVMCONbits.WRERR = 0;
    NVMCONbits.WREN = 1;
    NVMCONbits.NVMOP = NVMOP_PAGE_ERASE;
    NVMCONbits.WR = 1;
    return WaitForCompletion();
}

bool NVM_FLASH_WriteBytes(size_t offset, const uint8_t *data, size_t length)
{
    if (((offset % NVM_FLASH_WORD_SIZE) != 0U) || ((length % NVM_FLASH_WORD_SIZE) != 0U)) {
        return false;
    }
    for (size_t written = 0; written < length; written += NVM_FLASH_WORD_SIZE) {
        uint32_t word[4];
        memcpy(word, data + written, NVM_FLASH_WORD_SIZE);
        NVMDATA0 = word[0];
        NVMDATA1 = word[1];
        NVMDATA2 = word[2];
        NVMDATA3 = word[3];
        NVMADR = NVM_FLASH_CONFIG_PAGE_ADDR + offset + written;
        NVMCONbits.WRERR = 0;
        NVMCONbits.WREN = 1;
        NVMCONbits.NVMOP = NVMOP_WORD_PROGRAM;
        NVMCONbits.WR = 1;
        if (!WaitForCompletion()) {
            return false;
        }
    }
    return true;
}

void NVM_FLASH_ReadBytes(size_t offset, uint8_t *data, size_t length)
{
    // Program flash is memory-mapped for reads on this core (DS70005591
    // section 6.3.1), no table reads or PSV window needed.
    memcpy(data, (const uint8_t *)(NVM_FLASH_CONFIG_PAGE_ADDR + offset), length);
}
