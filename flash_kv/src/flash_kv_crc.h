/**
 * @file flash_kv_crc.h
 * @brief CRC 校验头文件
 * @description CRC16/CRC32 校验函数声明
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#ifndef FLASH_KV_CRC_H
#define FLASH_KV_CRC_H

#include <stdint.h>

uint16_t kv_crc16(const uint8_t *data, uint32_t len);
uint32_t kv_crc32(const uint8_t *data, uint32_t len);

#endif
