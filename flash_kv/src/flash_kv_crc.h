#ifndef FLASH_KV_CRC_H
#define FLASH_KV_CRC_H

#include <stdint.h>

uint16_t kv_crc16(const uint8_t *data, uint32_t len);
uint32_t kv_crc32(const uint8_t *data, uint32_t len);

#endif
