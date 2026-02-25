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
