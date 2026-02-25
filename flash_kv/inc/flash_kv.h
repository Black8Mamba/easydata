#ifndef FLASH_KV_H
#define FLASH_KV_H

#include "flash_kv_config.h"
#include "flash_kv_types.h"

int flash_kv_adapter_register(const flash_kv_ops_t *ops);
const flash_kv_ops_t* flash_kv_adapter_get(void);

int flash_kv_init(uint8_t instance_id, const kv_instance_config_t *config);
kv_handle_t* flash_kv_get_handle(uint8_t instance_id);
int flash_kv_deinit(uint8_t instance_id);

int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len);
int flash_kv_get(const uint8_t *key, uint8_t key_len,
                 uint8_t *value, uint8_t *value_len);
int flash_kv_del(const uint8_t *key, uint8_t key_len);
bool flash_kv_exists(const uint8_t *key, uint8_t key_len);

int flash_kv_tx_begin(void);
int flash_kv_tx_commit(void);
int flash_kv_tx_rollback(void);

int flash_kv_gc(void);
uint8_t flash_kv_free_percent(void);

typedef int (*kv_foreach_cb)(const uint8_t *key, uint8_t key_len,
                             const uint8_t *value, uint8_t value_len,
                             void *user_data);
int flash_kv_foreach(kv_foreach_cb callback, void *user_data);

int flash_kv_clear(void);
uint32_t flash_kv_count(void);
int flash_kv_status(uint32_t *total, uint32_t *used);

#endif
