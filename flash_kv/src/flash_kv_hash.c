/**
 * @file flash_kv_hash.c
 * @brief 哈希表实现
 * @description 使用DJB2哈希函数和开放地址法实现O(1)查找的哈希表
 *              使用墓碑(tombstone)机制正确处理删除操作
 * @author EasyData
 * @date 2026-02-25
 * @version 1.1.0
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

/* 判断slot是否被占用 (有效key) */
static int kv_hash_slot_occupied(const kv_hash_slot_t *slot)
{
    return (slot->key_len != KV_HASH_SLOT_EMPTY &&
            slot->key_len != KV_HASH_SLOT_TOMBSTONE);
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

        if (slot->key_len == KV_HASH_SLOT_EMPTY) {
            /* 遇到空槽, 说明key不存在 */
            return -1;
        }

        if (slot->key_len == KV_HASH_SLOT_TOMBSTONE) {
            /* 遇到墓碑, 继续探测 */
            continue;
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
    int first_tombstone = -1;

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);
        kv_hash_slot_t *slot = &table->slots[idx];

        if (slot->key_len == KV_HASH_SLOT_TOMBSTONE) {
            /* 记录第一个墓碑位置, 可用于插入 */
            if (first_tombstone < 0) {
                first_tombstone = idx;
            }
            continue;
        }

        if (slot->key_len == KV_HASH_SLOT_EMPTY) {
            /* 找到空槽, key不存在, 插入到墓碑位置或此空槽 */
            uint16_t insert_idx = (first_tombstone >= 0) ?
                                  (uint16_t)first_tombstone : idx;
            kv_hash_slot_t *insert_slot = &table->slots[insert_idx];
            insert_slot->key_len = key_len;
            memcpy(insert_slot->key, key, key_len);
            insert_slot->flash_offset = offset;
            table->count++;
            return 0;
        }

        if (slot->key_len == key_len && memcmp(slot->key, key, key_len) == 0) {
            /* key已存在, 更新offset */
            slot->flash_offset = offset;
            return 0;
        }
    }

    /* 哈希表满 */
    return -1;
}

/* 哈希表删除 (使用墓碑标记) */
int kv_hash_del(kv_hash_table_t *table, const uint8_t *key, uint8_t key_len)
{
    uint16_t hash = kv_hash_djb2(key, key_len);

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);
        kv_hash_slot_t *slot = &table->slots[idx];

        if (slot->key_len == KV_HASH_SLOT_EMPTY) {
            return -1;
        }

        if (slot->key_len == KV_HASH_SLOT_TOMBSTONE) {
            continue;
        }

        if (slot->key_len == key_len && memcmp(slot->key, key, key_len) == 0) {
            /* 标记为墓碑而非清空, 保持探测链完整 */
            slot->key_len = KV_HASH_SLOT_TOMBSTONE;
            memset(slot->key, 0, FLASH_KV_KEY_SIZE);
            table->count--;
            return 0;
        }
    }
    return -1;
}
