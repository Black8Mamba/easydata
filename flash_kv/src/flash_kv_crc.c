/**
 * @file flash_kv_crc.c
 * @brief CRC 校验实现
 * @description 提供CRC-16和CRC-32校验算法，用于数据完整性验证
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#include "flash_kv_crc.h"

/* CRC-16-CCITT 计算 */
uint16_t kv_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

/* CRC-32 计算 */
uint32_t kv_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ 0x04C11DB7;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}
