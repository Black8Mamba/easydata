/**
 * @file stm32_flash.c
 * @brief STM32 Flash适配器实现
 * @description 提供基于STM32 HAL库的Flash操作接口实现
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#include "stm32_flash.h"
#include "flash_kv_config.h"
#include "flash_kv_types.h"
#include <string.h>

/* STM32 Flash基地址 (根据具体型号修改) */
#if defined(STM32F1xx)
#define FLASH_BASE_ADDR     FLASH_BASE
#define FLASH_BANK_SIZE     (128 * 1024)  /* 128KB Flash */
#elif defined(STM32F4xx)
#define FLASH_BASE_ADDR     FLASH_BASE
#define FLASH_BANK_SIZE     (1024 * 1024)  /* 1MB Flash */
#elif defined(STM32L4xx)
#define FLASH_BASE_ADDR     FLASH_BASE
#define FLASH_BANK_SIZE     (512 * 1024)  /* 512KB Flash */
#else
#error "Unsupported STM32 series"
#endif

/* Flash页大小 (STM32F1xx: 2KB, STM32F4xx: 16KB/32KB/64KB, STM32L4xx: 4KB) */
#if defined(STM32F1xx)
#define FLASH_PAGE_SIZE     (2 * 1024)
#elif defined(STM32F4xx)
#define FLASH_PAGE_SIZE     (16 * 1024)
#elif defined(STM32L4xx)
#define FLASH_PAGE_SIZE     (4 * 1024)
#endif

/* KV存储区域配置 (需要在链接脚本中预留) */
extern uint32_t _flash_kv_start;
extern uint32_t _flash_kv_end;

static int stm32_flash_init(void)
{
    /* STM32 Flash无需额外初始化，HAL库会自动处理 */
    /* 可以在这里添加Flash解锁等操作 */
    return 0;
}

/**
 * @brief 读取Flash数据
 * @param addr 绝对Flash地址
 * @param buf 读取缓冲区
 * @param len 读取长度
 * @return 0成功，其他失败
 */
static int stm32_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr < FLASH_BASE_ADDR) {
        /* 转换为绝对地址 */
        addr = FLASH_BASE_ADDR + addr;
    }

    /* 直接内存拷贝，Flash可像RAM一样读取 */
    memcpy(buf, (void *)addr, len);
    return 0;
}

/**
 * @brief 写入Flash数据
 * @param addr 绝对Flash地址
 * @param buf 写入数据
 * @param len 写入长度
 * @return 0成功，其他失败
 *
 * @note STM32写入要求:
 *       1. 必须是半字(16bit)对齐写入
 *       2. 写入前必须先擦除(只能将1改为0)
 *       3. 需要解锁Flash才能写入
 */
static int stm32_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    HAL_StatusTypeDef status;
    uint32_t flash_addr;
    uint16_t halfword;
    uint32_t i;

    if (addr < FLASH_BASE_ADDR) {
        flash_addr = FLASH_BASE_ADDR + addr;
    } else {
        flash_addr = addr;
    }

    /* 解锁Flash */
    HAL_FLASH_Unlock();

    /* 按半字(16bit)写入 */
    for (i = 0; i < len; i += 2) {
        if (i + 1 < len) {
            /* 完整的半字 */
            halfword = buf[i] | (buf[i + 1] << 8);
        } else {
            /* 最后一个字节，补0xFF */
            halfword = buf[i] | 0xFF00;
        }

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                   flash_addr + i, halfword);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

/**
 * @brief 擦除Flash区域
 * @param addr 擦除起始地址(必须页对齐)
 * @param len 擦除长度
 * @return 0成功，其他失败
 *
 * @note STM32擦除特点:
 *       1. STM32F1只能按页擦除
 *       2. STM32F4/L4支持扇区擦除
 *       3. 擦除后所有位变为1
 */
static int stm32_flash_erase(uint32_t addr, uint32_t len)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error;
    uint32_t flash_addr;
    uint32_t pages;

    if (addr < FLASH_BASE_ADDR) {
        flash_addr = FLASH_BASE_ADDR + addr;
    } else {
        flash_addr = addr;
    }

    /* 计算需要擦除的页数 */
    pages = (len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

    /* 解锁Flash */
    HAL_FLASH_Unlock();

#if defined(STM32F1xx)
    /* STM32F1: 按页擦除 */
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = flash_addr;
    erase_init.NbPages = pages;
#elif defined(STM32F4xx) || defined(STM32L4xx)
    /* STM32F4/L4: 按扇区擦除 */
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = STM32FLASH_GetSector(flash_addr);
    erase_init.NbSectors = pages;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
#endif

    status = HAL_FLASH_Erase(&erase_init, &page_error);

    HAL_FLASH_Lock();

    if (status != HAL_OK) {
        return -1;
    }

    return 0;
}

#if defined(STM32F4xx) || defined(STM32L4xx)
/**
 * @brief 获取扇区号
 * @param addr Flash地址
 * @return 扇区号
 */
static uint32_t STM32FLASH_GetSector(uint32_t addr)
{
#if defined(STM32F4xx)
    if (addr < ADDR_FLASH_SECTOR_1) {
        return FLASH_SECTOR_0;
    } else if (addr < ADDR_FLASH_SECTOR_2) {
        return FLASH_SECTOR_1;
    } else if (addr < ADDR_FLASH_SECTOR_3) {
        return FLASH_SECTOR_2;
    } else if (addr < ADDR_FLASH_SECTOR_4) {
        return FLASH_SECTOR_3;
    } else if (addr < ADDR_FLASH_SECTOR_5) {
        return FLASH_SECTOR_4;
    } else {
        return FLASH_SECTOR_5;
    }
#elif defined(STM32L4xx)
    if (addr < FLASH_PAGE_SIZE) {
        return FLASH_PAGE_0;
    } else if (addr < 2 * FLASH_PAGE_SIZE) {
        return FLASH_PAGE_1;
    } else if (addr < 3 * FLASH_PAGE_SIZE) {
        return FLASH_PAGE_2;
    } else if (addr < 4 * FLASH_PAGE_SIZE) {
        return FLASH_PAGE_3;
    } else if (addr < 5 * FLASH_PAGE_SIZE) {
        return FLASH_PAGE_4;
    } else {
        return FLASH_PAGE_5;
    }
#endif
}
#endif

/* Flash KV操作接口 */
const flash_kv_ops_t stm32_flash_ops = {
    .init   = stm32_flash_init,
    .read   = stm32_flash_read,
    .write  = stm32_flash_write,
    .erase  = stm32_flash_erase,
};
