/**
 * @file stm32_flash.h
 * @brief STM32 Flash适配器头文件
 * @description STM32 HAL库Flash操作接口声明
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#ifndef STM32_FLASH_H
#define STM32_FLASH_H

#include <stdint.h>
#include "stm32f4xx_hal.h"  /* 根据实际型号修改 */

/* STM32 Flash操作接口 */
extern const flash_kv_ops_t stm32_flash_ops;

/* Flash地址定义 (根据具体型号和链接脚本修改) */
#define KV_FLASH_START_ADDR     0x0800F000  /* Flash KV存储起始地址 */
#define KV_FLASH_SIZE           (32 * 1024) /* 32KB */

#endif /* STM32_FLASH_H */
