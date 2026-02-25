#ifndef FLASH_KV_TYPES_H
#define FLASH_KV_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "flash_kv_config.h"

/*============================================================================
 * 错误码定义
 *============================================================================*/
typedef enum {
    KV_OK = 0,
    KV_ERR_INVALID_PARAM = -1,
    KV_ERR_NO_SPACE = -2,
    KV_ERR_NOT_FOUND = -3,
    KV_ERR_CRC_FAIL = -4,
    KV_ERR_FLASH_FAIL = -5,
    KV_ERR_TRANSACTION = -6,
    KV_ERR_NO_INIT = -7,
    KV_ERR_GC_FAIL = -8,
    KV_ERR_INVALID_REGION = -9,
    KV_ERR_HASH_FULL = -10
} kv_err_t;

/*============================================================================
 * Flash 操作接口 (用户实现)
 *============================================================================*/
typedef struct {
    int (*init)(void);
    int (*read)(uint32_t addr, uint8_t *buf, uint32_t len);
    int (*write)(uint32_t addr, const uint8_t *buf, uint32_t len);
    int (*erase)(uint32_t addr, uint32_t len);
} flash_kv_ops_t;

/*============================================================================
 * KV 实例配置
 *============================================================================*/
typedef struct {
    uint32_t start_addr;
    uint32_t total_size;
    uint32_t block_size;
    const flash_kv_ops_t *ops;
} kv_instance_config_t;

/*============================================================================
 * 魔术字定义
 *============================================================================*/
#define KV_MAGIC              0x4B565341
#define KV_MAGIC_B            0x4B565342

/*============================================================================
 * 事务状态 (持久化到Flash)
 *============================================================================*/
typedef enum {
    KV_TX_STATE_IDLE = 0,
    KV_TX_STATE_PREPARED = 1,
    KV_TX_STATE_COMMITTED = 2
} kv_tx_state_persist_t;

/*============================================================================
 * KV 句柄
 *============================================================================*/
typedef struct kv_handle {
    uint8_t instance_id;
    uint32_t active_region;
    uint32_t version;
    uint32_t record_count;
    kv_tx_state_persist_t tx_state;
    uint32_t region_addr[2];
    uint32_t region_size;
    uint32_t block_size;
    const flash_kv_ops_t *ops;
} kv_handle_t;

/*============================================================================
 * 记录结构
 *============================================================================*/
typedef struct {
    uint8_t key[FLASH_KV_KEY_SIZE];
    uint8_t value[FLASH_KV_VALUE_SIZE];
    uint8_t flags;
    uint8_t reserved[3];
    uint16_t crc16;
} __attribute__((packed)) kv_record_t;

/*============================================================================
 * 区域头部
 *============================================================================*/
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t record_count;
    uint32_t active_offset;
    uint8_t  tx_state;
    uint8_t  reserved[3];
    uint32_t crc32;
} __attribute__((packed)) kv_region_header_t;

/*============================================================================
 * 哈希表槽
 *============================================================================*/
typedef struct {
    uint8_t  key_len;
    uint8_t  key[FLASH_KV_KEY_SIZE];
    uint32_t flash_offset;
} kv_hash_slot_t;

typedef struct {
    kv_hash_slot_t slots[FLASH_KV_HASH_SIZE];
    uint16_t count;
} kv_hash_table_t;

#endif /* FLASH_KV_TYPES_H */
