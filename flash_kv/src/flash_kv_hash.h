/**
 * @file flash_kv_hash.h
 * @brief 哈希表接口头文件
 * @description 哈希表操作接口声明
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#ifndef FLASH_KV_HASH_H
#define FLASH_KV_HASH_H

#include "flash_kv_types.h"

void kv_hash_init(kv_hash_table_t *table);
int kv_hash_get(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len,
                uint32_t *offset);
int kv_hash_set(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len,
                uint32_t offset);
int kv_hash_del(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len);

#endif
