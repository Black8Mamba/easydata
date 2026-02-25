#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "flash_kv_config.h"
#include "flash_kv_types.h"

typedef struct {
    uint8_t *memory;
    uint32_t size;
    uint32_t block_size;
} mem_flash_t;

static mem_flash_t g_flash = {0};

static int mem_flash_init(void)
{
    g_flash.size = 64 * 1024;  /* 64KB for testing */
    g_flash.block_size = FLASH_KV_BLOCK_SIZE;
    g_flash.memory = calloc(g_flash.size, 1);
    if (g_flash.memory == NULL) {
        return -1;
    }
    return 0;
}

static int mem_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr + len > g_flash.size) {
        return -1;
    }
    memcpy(buf, g_flash.memory + addr, len);
    return 0;
}

static int mem_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (addr + len > g_flash.size) {
        return -1;
    }
    /* 模拟Flash写入: 只能将1写成0 */
    for (uint32_t i = 0; i < len; i++) {
        g_flash.memory[addr + i] &= buf[i];
    }
    return 0;
}

static int mem_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t block_start = addr / g_flash.block_size;
    uint32_t block_end = (addr + len + g_flash.block_size - 1) / g_flash.block_size;

    for (uint32_t i = block_start; i < block_end; i++) {
        memset(g_flash.memory + i * g_flash.block_size, 0xFF, g_flash.block_size);
    }
    return 0;
}

const flash_kv_ops_t mock_flash_ops = {
    .init   = mem_flash_init,
    .read   = mem_flash_read,
    .write  = mem_flash_write,
    .erase  = mem_flash_erase,
};
