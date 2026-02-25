/**
 * @file flash_kv_hash.c
 * @brief 哈希表实现
 * @description 使用DJB2哈希函数和开放地址法实现O(1)查找的哈希表
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#include <string.h>
#include "flash_kv_hash.h"

/* DJB2 哈希函数 */
static uint16_t kv_hash_djb2(const uint8_t *key, uint8_t len)
{
    uint32_t hash = 5381;
    for (uint8_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + key[i];
    }
    return hash & (FLASH_KV_HASH_SIZE - 1);
}

/* 哈希表初始化 */
void kv_hash_init(kv_hash_table_t *table)
{
    memset(table, 0, sizeof(kv_hash_table_t));
}

/* 哈希表查找 */
int kv_hash_get(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len,
                uint32_t *offset)
{
    uint16_t hash = kv_hash_djb2(key, key_len);

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);
        kv_hash_slot_t *slot = &table->slots[idx];

        if (slot->key_len == 0) {
            return -1;
        }

        if (slot->key_len == key_len && memcmp(slot->key, key, key_len) == 0) {
            *offset = slot->flash_offset;
            return 0;
        }
    }
    return -1;
}

/* 哈希表插入/更新 */
int kv_hash_set(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len,
                uint32_t offset)
{
    uint16_t hash = kv_hash_djb2(key, key_len);

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);
        kv_hash_slot_t *slot = &table->slots[idx];

        if (slot->key_len == 0 || (slot->key_len == key_len &&
            memcmp(slot->key, key, key_len) == 0)) {
            slot->key_len = key_len;
            memcpy(slot->key, key, key_len);
            slot->flash_offset = offset;
            if (slot->key_len == 0) {
                table->count++;
            }
            return 0;
        }
    }
    return -1;
}

/* 哈希表删除 */
int kv_hash_del(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len)
{
    uint16_t hash = kv_hash_djb2(key, key_len);

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);
        kv_hash_slot_t *slot = &table->slots[idx];

        if (slot->key_len == 0) {
            return -1;
        }

        if (slot->key_len == key_len && memcmp(slot->key, key, key_len) == 0) {
            slot->key_len = 0;
            memset(slot->key, 0, FLASH_KV_KEY_SIZE);  /* 清除key数据 */
            table->count--;
            return 0;
        }
    }
    return -1;
}
