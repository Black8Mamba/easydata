/**
 * @file flash_kv_utils.c
 * @brief 类型转换辅助函数实现
 * @description 提供常用数据类型(u8/i8/u16/u32/float/double/bool)与字节数组的相互转换
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#include <string.h>
#include "flash_kv_utils.h"

/* uint8_t */
void kv_put_u8(uint8_t *buf, uint8_t val)
{
    buf[0] = val;
}

uint8_t kv_get_u8(const uint8_t *buf)
{
    return buf[0];
}

/* int8_t */
void kv_put_i8(uint8_t *buf, int8_t val)
{
    buf[0] = (uint8_t)val;
}

int8_t kv_get_i8(const uint8_t *buf)
{
    return (int8_t)buf[0];
}

/* uint16_t 大端 */
void kv_put_u16be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFF);
}

uint16_t kv_get_u16be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

/* uint16_t 小端 */
void kv_put_u16le(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)(val >> 8);
}

uint16_t kv_get_u16le(const uint8_t *buf)
{
    return ((uint16_t)buf[1] << 8) | buf[0];
}

/* uint32_t 大端 */
void kv_put_u32be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

uint32_t kv_get_u32be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | buf[3];
}

/* uint32_t 小端 */
void kv_put_u32le(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)(val >> 24);
}

uint32_t kv_get_u32le(const uint8_t *buf)
{
    return ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[1] << 8) | buf[0];
}

/* float */
void kv_put_float(uint8_t *buf, float val)
{
    memcpy(buf, &val, sizeof(float));
}

float kv_get_float(const uint8_t *buf)
{
    float val;
    memcpy(&val, buf, sizeof(float));
    return val;
}

/* double */
void kv_put_double(uint8_t *buf, double val)
{
    memcpy(buf, &val, sizeof(double));
}

double kv_get_double(const uint8_t *buf)
{
    double val;
    memcpy(&val, buf, sizeof(double));
    return val;
}

/* bool */
void kv_put_bool(uint8_t *buf, bool val)
{
    buf[0] = val ? 1 : 0;
}

bool kv_get_bool(const uint8_t *buf)
{
    return buf[0] ? true : false;
}
