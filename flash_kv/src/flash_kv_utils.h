/**
 * @file flash_kv_utils.h
 * @brief 工具函数头文件
 * @description 字节序转换和类型序列化工具函数声明
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#ifndef FLASH_KV_UTILS_H
#define FLASH_KV_UTILS_H

#include <stdint.h>
#include <stdbool.h>

void kv_put_u8(uint8_t *buf, uint8_t val);
uint8_t kv_get_u8(const uint8_t *buf);

void kv_put_i8(uint8_t *buf, int8_t val);
int8_t kv_get_i8(const uint8_t *buf);

void kv_put_u16be(uint8_t *buf, uint16_t val);
uint16_t kv_get_u16be(const uint8_t *buf);

void kv_put_u16le(uint8_t *buf, uint16_t val);
uint16_t kv_get_u16le(const uint8_t *buf);

void kv_put_u32be(uint8_t *buf, uint32_t val);
uint32_t kv_get_u32be(const uint8_t *buf);

void kv_put_u32le(uint8_t *buf, uint32_t val);
uint32_t kv_get_u32le(const uint8_t *buf);

void kv_put_float(uint8_t *buf, float val);
float kv_get_float(const uint8_t *buf);

void kv_put_double(uint8_t *buf, double val);
double kv_get_double(const uint8_t *buf);

void kv_put_bool(uint8_t *buf, bool val);
bool kv_get_bool(const uint8_t *buf);

#endif
