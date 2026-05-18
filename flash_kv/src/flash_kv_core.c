/**
 * @file flash_kv_core.c
 * @brief Flash KV 核心实现
 * @description 提供基于Flash的Key-Value存储核心功能，包括:
 *             - 基本的KV操作 (set/get/del/exists)
 *             - 垃圾回收 (GC)
 *             - 事务支持 (begin/commit/rollback)
 *             - 双区域备份 (A/B区域切换)
 * @author EasyData
 * @date 2026-02-25
 * @version 1.1.0
 */

#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "flash_kv.h"
#include "flash_kv_hash.h"
#include "flash_kv_crc.h"

/* 全局句柄 */
static kv_handle_t g_handles[FLASH_KV_INSTANCE_MAX];
static kv_hash_table_t g_hash_table;
static const flash_kv_ops_t *g_flash_ops = NULL;
static uint8_t g_initialized = 0;

/* 事务相关变量 */
typedef struct {
    kv_record_t record;
    uint8_t is_delete;  /* 1=删除操作, 0=写入操作 */
} kv_tx_entry_t;

static kv_tx_entry_t g_tx_buffer[FLASH_KV_TX_MAX_RECORDS];
static uint8_t g_tx_count = 0;
static uint8_t g_tx_active = 0;  /* 0: 无事务, 1: 事务进行中 */

/* 前向声明 */
static void kv_hash_rebuild(kv_handle_t *handle);
static int kv_region_header_write(kv_handle_t *handle, uint8_t region);

/*============================================================================
 * 区域头部操作
 *============================================================================*/

/* 读取区域头部 */
static int kv_region_header_read(kv_handle_t *handle, uint8_t region,
                                kv_region_header_t *header)
{
    if (handle->ops->read(handle->region_addr[region],
                         (uint8_t *)header, sizeof(*header)) != 0) {
        return -1;
    }
    return 0;
}

/* 验证区域头部有效性 */
static int kv_region_header_valid(const kv_region_header_t *header)
{
    if (header->magic != KV_MAGIC && header->magic != KV_MAGIC_B) {
        return -1;
    }

    uint32_t crc = kv_crc32((const uint8_t *)header,
                           sizeof(kv_region_header_t) - 4);
    if (crc != header->crc32) {
        return -1;
    }

    return 0;
}

/* 写入区域头部到Flash (要求该位置已被擦除或可覆写) */
static int kv_region_header_write(kv_handle_t *handle, uint8_t region)
{
    kv_region_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = (region == 0) ? KV_MAGIC : KV_MAGIC_B;
    header.version = handle->version;
    header.record_count = handle->record_count;
    header.active_offset = handle->active_offset;
    header.tx_state = (uint8_t)handle->tx_state;
    header.crc32 = kv_crc32((const uint8_t *)&header,
                            sizeof(kv_region_header_t) - 4);

    return handle->ops->write(handle->region_addr[region],
                             (const uint8_t *)&header, sizeof(header));
}

/* 初始化区域 (擦除 + 写头部) */
static int kv_region_format(kv_handle_t *handle, uint8_t region)
{
    if (handle->ops->erase(handle->region_addr[region], handle->region_size) != 0) {
        return -1;
    }

    handle->version = 1;
    handle->record_count = 0;
    handle->active_offset = sizeof(kv_region_header_t);
    handle->tx_state = KV_TX_STATE_IDLE;

    return kv_region_header_write(handle, region);
}

/*============================================================================
 * 适配器注册
 *============================================================================*/

int flash_kv_adapter_register(const flash_kv_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL || ops->read == NULL ||
        ops->write == NULL || ops->erase == NULL) {
        return KV_ERR_INVALID_PARAM;
    }
    g_flash_ops = ops;
    return ops->init();
}

const flash_kv_ops_t* flash_kv_adapter_get(void)
{
    return g_flash_ops;
}

/*============================================================================
 * 初始化
 *============================================================================*/

int flash_kv_init(uint8_t instance_id, const kv_instance_config_t *config)
{
    if (instance_id >= FLASH_KV_INSTANCE_MAX || config == NULL) {
        return KV_ERR_INVALID_PARAM;
    }

    if (g_flash_ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    kv_handle_t *handle = &g_handles[instance_id];
    memset(handle, 0, sizeof(kv_handle_t));

    handle->instance_id = instance_id;
    handle->ops = config->ops ? config->ops : g_flash_ops;
    handle->region_addr[0] = config->start_addr;
    handle->region_size = config->total_size / 2;
    handle->block_size = config->block_size;
    handle->region_addr[1] = config->start_addr + handle->region_size;
    handle->active_region = 0;
    handle->version = 1;
    handle->active_offset = sizeof(kv_region_header_t);

    /* 读取两个区域的头部 */
    kv_region_header_t header0, header1;
    int read0 = kv_region_header_read(handle, 0, &header0);
    int read1 = kv_region_header_read(handle, 1, &header1);

    int valid0 = (read0 == 0 && kv_region_header_valid(&header0) == 0);
    int valid1 = (read1 == 0 && kv_region_header_valid(&header1) == 0);

    if (valid0 && valid1) {
        /* 两个区域都有效，选择版本号更大的 */
        if (header1.version > header0.version) {
            handle->active_region = 1;
            handle->version = header1.version;
        } else {
            handle->active_region = 0;
            handle->version = header0.version;
        }
    } else if (valid0) {
        handle->active_region = 0;
        handle->version = header0.version;
    } else if (valid1) {
        handle->active_region = 1;
        handle->version = header1.version;
    } else {
        /* 两个区域都无效，格式化区域0 */
        kv_region_format(handle, 0);
        handle->active_region = 0;
    }

    /* 断电恢复: 检查事务状态 */
    if (valid0 || valid1) {
        kv_region_header_t *active_header =
            (handle->active_region == 0) ? &header0 : &header1;
        if (active_header->tx_state == KV_TX_STATE_PREPARED) {
            /* 上次有未完成的事务, rebuild会自然跳过无效记录 */
        }
    }

    /* 重建哈希表 (扫描Flash恢复内存索引) */
    kv_hash_rebuild(handle);

    /* 重置事务状态 */
    handle->tx_state = KV_TX_STATE_IDLE;
    g_tx_active = 0;
    g_tx_count = 0;

    g_initialized = 1;
    return KV_OK;
}

kv_handle_t* flash_kv_get_handle(uint8_t instance_id)
{
    if (instance_id >= FLASH_KV_INSTANCE_MAX || !g_initialized) {
        return NULL;
    }
    return &g_handles[instance_id];
}

int flash_kv_deinit(uint8_t instance_id)
{
    if (instance_id >= FLASH_KV_INSTANCE_MAX) {
        return KV_ERR_INVALID_PARAM;
    }
    g_initialized = 0;
    return KV_OK;
}

/*============================================================================
 * 记录操作
 *============================================================================*/

/* 验证记录CRC */
static int kv_record_check_crc(const kv_record_t *record)
{
    uint16_t crc = kv_crc16((const uint8_t *)record,
                            sizeof(kv_record_t) - 2);
    return (crc == record->crc16) ? 0 : -1;
}

/* 重建哈希表 - 从Flash扫描有效记录, 同时确定active_offset */
static void kv_hash_rebuild(kv_handle_t *handle)
{
    kv_hash_init(&g_hash_table);
    handle->record_count = 0;

    uint32_t region_addr = handle->region_addr[handle->active_region];
    uint32_t offset = sizeof(kv_region_header_t);
    uint32_t max_offset = handle->region_size - handle->block_size;
    kv_record_t record;

    while (offset + sizeof(kv_record_t) <= max_offset) {
        if (handle->ops->read(region_addr + offset, (uint8_t *)&record,
                             sizeof(record)) != 0) {
            break;
        }

        /* 如果flags是擦除态, 说明后面没有更多记录 */
        if (record.flags == KV_FLAG_ERASED) {
            break;
        }

        if (record.flags == KV_FLAG_VALID && kv_record_check_crc(&record) == 0) {
            /* 有效记录: 加入哈希表 (后面的同key会覆盖前面的) */
            uint32_t existing_offset;
            if (kv_hash_get(&g_hash_table, record.key, record.key_len,
                           &existing_offset) == 0) {
                /* key已存在, 更新 (不增加计数) */
                kv_hash_set(&g_hash_table, record.key, record.key_len, offset);
            } else {
                /* 新key */
                kv_hash_set(&g_hash_table, record.key, record.key_len, offset);
                handle->record_count++;
            }
        } else if (record.flags == KV_FLAG_DELETED) {
            /* 已删除的记录: 如果哈希表中有对应key, 移除 */
            uint32_t existing_offset;
            if (kv_hash_get(&g_hash_table, record.key, record.key_len,
                           &existing_offset) == 0) {
                kv_hash_del(&g_hash_table, record.key, record.key_len);
                handle->record_count--;
            }
        }
        /* CRC失败的记录跳过 */

        offset += sizeof(kv_record_t);
    }

    handle->active_offset = offset;
}

/* 写入记录到Flash (计算CRC后写入) */
static int kv_record_write(kv_handle_t *handle, uint32_t abs_addr,
                          const kv_record_t *record)
{
    kv_record_t r = *record;
    r.crc16 = kv_crc16((const uint8_t *)&r, sizeof(kv_record_t) - 2);
    return handle->ops->write(abs_addr, (const uint8_t *)&r, sizeof(r));
}

/* 标记记录为已删除 (只清零flags中的bit, Flash兼容) */
static int kv_record_mark_deleted(kv_handle_t *handle, uint32_t rel_offset)
{
    uint32_t region_addr = handle->region_addr[handle->active_region];
    uint32_t flags_addr = region_addr + rel_offset +
                          offsetof(kv_record_t, flags);

    /*
     * 写入KV_FLAG_DELETED(0xFC)到flags位置
     * Flash AND运算: 原值0xFE & 写入0xFC = 0xFC
     * bit0: 0 & 0 = 0 (不变)
     * bit1: 1 & 0 = 0 (从1变0, Flash允许)
     */
    uint8_t del_flag = KV_FLAG_DELETED;
    return handle->ops->write(flags_addr, &del_flag, 1);
}

/*============================================================================
 * 基本KV操作
 *============================================================================*/

int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len)
{
    if (key == NULL || value == NULL || key_len == 0 ||
        key_len > FLASH_KV_KEY_SIZE || value_len > FLASH_KV_VALUE_SIZE) {
        return KV_ERR_INVALID_PARAM;
    }

    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    /* 如果在事务中, 缓存到事务缓冲区 */
    if (g_tx_active) {
        if (g_tx_count >= FLASH_KV_TX_MAX_RECORDS) {
            return KV_ERR_NO_SPACE;
        }
        kv_tx_entry_t *entry = &g_tx_buffer[g_tx_count];
        memset(&entry->record, 0, sizeof(kv_record_t));
        memcpy(entry->record.key, key, key_len);
        memcpy(entry->record.value, value, value_len);
        entry->record.key_len = key_len;
        entry->record.value_len = value_len;
        entry->record.flags = KV_FLAG_VALID;
        entry->is_delete = 0;
        g_tx_count++;
        return KV_OK;
    }

    /* 检查key是否已存在 */
    uint32_t old_offset;
    int key_exists = (kv_hash_get(&g_hash_table, key, key_len, &old_offset) == 0);

    /* 检查空间 */
    uint32_t max_offset = handle->region_size - handle->block_size;

    if (handle->active_offset + sizeof(kv_record_t) > max_offset) {
        /* 空间不足，尝试GC */
        int gc_ret = flash_kv_gc();
        if (gc_ret != KV_OK) {
            return KV_ERR_NO_SPACE;
        }
        /* GC后检查空间 */
        if (handle->active_offset + sizeof(kv_record_t) > max_offset) {
            return KV_ERR_NO_SPACE;
        }
    }

    /* 如果key已存在, 标记旧记录为已删除 */
    if (key_exists) {
        kv_record_mark_deleted(handle, old_offset);
    }

    /* 构造新记录 */
    kv_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.key, key, key_len);
    memcpy(record.value, value, value_len);
    record.key_len = key_len;
    record.value_len = value_len;
    record.flags = KV_FLAG_VALID;

    /* 写入新记录 */
    uint32_t region_addr = handle->region_addr[handle->active_region];
    uint32_t write_addr = region_addr + handle->active_offset;
    int ret = kv_record_write(handle, write_addr, &record);
    if (ret != 0) {
        return KV_ERR_FLASH_FAIL;
    }

    /* 更新哈希表 */
    kv_hash_set(&g_hash_table, key, key_len, handle->active_offset);

    /* 更新内存状态 */
    if (!key_exists) {
        handle->record_count++;
    }
    handle->active_offset += sizeof(kv_record_t);

    return KV_OK;
}

int flash_kv_get(const uint8_t *key, uint8_t key_len,
                 uint8_t *value, uint8_t *value_len)
{
    if (key == NULL || value == NULL || value_len == NULL) {
        return KV_ERR_INVALID_PARAM;
    }

    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    /* 如果在事务中, 先查缓冲区 (后面的覆盖前面的) */
    if (g_tx_active) {
        for (int i = g_tx_count - 1; i >= 0; i--) {
            kv_tx_entry_t *entry = &g_tx_buffer[i];
            if (entry->record.key_len == key_len &&
                memcmp(entry->record.key, key, key_len) == 0) {
                if (entry->is_delete) {
                    return KV_ERR_NOT_FOUND;
                }
                memset(value, 0, *value_len);
                memcpy(value, entry->record.value, entry->record.value_len);
                *value_len = entry->record.value_len;
                return KV_OK;
            }
        }
    }

    /* 查找哈希表 */
    uint32_t offset;
    if (kv_hash_get(&g_hash_table, key, key_len, &offset) != 0) {
        return KV_ERR_NOT_FOUND;
    }

    /* 读取记录 */
    uint32_t region_addr = handle->region_addr[handle->active_region];
    kv_record_t record;
    if (handle->ops->read(region_addr + offset, (uint8_t *)&record,
                         sizeof(record)) != 0) {
        return KV_ERR_FLASH_FAIL;
    }

    /* 验证CRC */
    if (kv_record_check_crc(&record) != 0) {
        return KV_ERR_CRC_FAIL;
    }

    /* 复制value */
    memset(value, 0, FLASH_KV_VALUE_SIZE);
    memcpy(value, record.value, record.value_len);
    *value_len = record.value_len;

    return KV_OK;
}

int flash_kv_del(const uint8_t *key, uint8_t key_len)
{
    if (key == NULL || key_len == 0) {
        return KV_ERR_INVALID_PARAM;
    }

    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    /* 如果在事务中, 缓存删除操作 */
    if (g_tx_active) {
        if (g_tx_count >= FLASH_KV_TX_MAX_RECORDS) {
            return KV_ERR_NO_SPACE;
        }
        kv_tx_entry_t *entry = &g_tx_buffer[g_tx_count];
        memset(&entry->record, 0, sizeof(kv_record_t));
        memcpy(entry->record.key, key, key_len);
        entry->record.key_len = key_len;
        entry->is_delete = 1;
        g_tx_count++;
        return KV_OK;
    }

    /* 查找哈希表获取Flash偏移量 */
    uint32_t offset;
    if (kv_hash_get(&g_hash_table, key, key_len, &offset) != 0) {
        return KV_ERR_NOT_FOUND;
    }

    /* 标记记录为已删除 (Flash兼容: 只清零bit) */
    kv_record_mark_deleted(handle, offset);

    /* 从哈希表删除 */
    kv_hash_del(&g_hash_table, key, key_len);
    handle->record_count--;

    return KV_OK;
}

bool flash_kv_exists(const uint8_t *key, uint8_t key_len)
{
    uint32_t offset;
    return (kv_hash_get(&g_hash_table, key, key_len, &offset) == 0);
}

/*============================================================================
 * 事务接口
 *============================================================================*/

int flash_kv_tx_begin(void)
{
    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    if (g_tx_active) {
        return KV_ERR_TRANSACTION;
    }

    handle->tx_state = KV_TX_STATE_PREPARED;
    g_tx_active = 1;
    g_tx_count = 0;

    return KV_OK;
}

int flash_kv_tx_commit(void)
{
    kv_handle_t *handle = &g_handles[0];
    if (!g_tx_active) {
        return KV_ERR_TRANSACTION;
    }

    /* 临时关闭事务标志, 让set/del直接写入Flash */
    g_tx_active = 0;

    for (uint8_t i = 0; i < g_tx_count; i++) {
        kv_tx_entry_t *entry = &g_tx_buffer[i];
        int ret;

        if (entry->is_delete) {
            ret = flash_kv_del(entry->record.key, entry->record.key_len);
            if (ret != KV_OK && ret != KV_ERR_NOT_FOUND) {
                handle->tx_state = KV_TX_STATE_IDLE;
                g_tx_count = 0;
                return ret;
            }
        } else {
            ret = flash_kv_set(entry->record.key, entry->record.key_len,
                              entry->record.value, entry->record.value_len);
            if (ret != KV_OK) {
                handle->tx_state = KV_TX_STATE_IDLE;
                g_tx_count = 0;
                return ret;
            }
        }
    }

    handle->tx_state = KV_TX_STATE_IDLE;
    g_tx_count = 0;
    return KV_OK;
}

int flash_kv_tx_rollback(void)
{
    kv_handle_t *handle = &g_handles[0];
    g_tx_active = 0;
    g_tx_count = 0;
    handle->tx_state = KV_TX_STATE_IDLE;
    return KV_OK;
}

/*============================================================================
 * GC - 垃圾回收
 *============================================================================*/

int flash_kv_gc(void)
{
    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    uint8_t active = handle->active_region;
    uint8_t inactive = 1 - active;
    uint32_t active_addr = handle->region_addr[active];
    uint32_t inactive_addr = handle->region_addr[inactive];

    /* 擦除备用区域 */
    if (handle->ops->erase(inactive_addr, handle->region_size) != 0) {
        return KV_ERR_FLASH_FAIL;
    }

    /* 扫描当前区域, 复制有效记录到备用区域 */
    uint32_t read_offset = sizeof(kv_region_header_t);
    uint32_t write_offset = sizeof(kv_region_header_t);
    uint32_t max_offset = handle->region_size - handle->block_size;
    kv_record_t record;
    uint32_t new_record_count = 0;

    /* 使用临时哈希表 */
    kv_hash_table_t new_hash_table;
    kv_hash_init(&new_hash_table);

    while (read_offset + sizeof(kv_record_t) <= max_offset) {
        if (handle->ops->read(active_addr + read_offset, (uint8_t *)&record,
                             sizeof(record)) != 0) {
            break;
        }

        /* 遇到擦除态, 没有更多记录 */
        if (record.flags == KV_FLAG_ERASED) {
            break;
        }

        /* 只复制有效记录 (CRC正确且未删除) */
        if (record.flags == KV_FLAG_VALID && kv_record_check_crc(&record) == 0) {
            /* 检查是否已有同key记录 (去重: 保留最新的) */
            uint32_t existing;
            if (kv_hash_get(&new_hash_table, record.key, record.key_len,
                           &existing) == 0) {
                /* 已存在, 后面的覆盖前面的 */
            } else {
                new_record_count++;
            }

            /* 写入到新区域 */
            if (kv_record_write(handle, inactive_addr + write_offset, &record) != 0) {
                return KV_ERR_FLASH_FAIL;
            }

            kv_hash_set(&new_hash_table, record.key, record.key_len,
                       write_offset);
            write_offset += sizeof(kv_record_t);
        }

        read_offset += sizeof(kv_record_t);
    }

    /* 切换活跃区域 */
    handle->active_region = inactive;
    handle->record_count = new_hash_table.count;
    handle->active_offset = write_offset;
    handle->version++;
    handle->tx_state = KV_TX_STATE_IDLE;

    /* 写入新区域头部 */
    if (kv_region_header_write(handle, inactive) != 0) {
        return KV_ERR_FLASH_FAIL;
    }

    /* 替换哈希表 */
    memcpy(&g_hash_table, &new_hash_table, sizeof(kv_hash_table_t));

    return KV_OK;
}

uint8_t flash_kv_free_percent(void)
{
    kv_handle_t *handle = &g_handles[0];
    uint32_t total = handle->region_size - handle->block_size -
                     sizeof(kv_region_header_t);
    uint32_t used = handle->active_offset - sizeof(kv_region_header_t);
    if (total == 0) return 0;
    if (used > total) return 0;
    return (uint8_t)((total - used) * 100 / total);
}

/*============================================================================
 * 遍历、清空、统计
 *============================================================================*/

int flash_kv_foreach(kv_foreach_cb callback, void *user_data)
{
    if (callback == NULL) {
        return KV_ERR_INVALID_PARAM;
    }

    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    uint32_t region_addr = handle->region_addr[handle->active_region];
    uint32_t offset = sizeof(kv_region_header_t);
    uint32_t max_offset = handle->region_size - handle->block_size;
    kv_record_t record;

    while (offset + sizeof(kv_record_t) <= max_offset) {
        if (handle->ops->read(region_addr + offset, (uint8_t *)&record,
                             sizeof(record)) != 0) {
            break;
        }

        if (record.flags == KV_FLAG_ERASED) {
            break;
        }

        if (record.flags == KV_FLAG_VALID && kv_record_check_crc(&record) == 0) {
            /* 确认该记录是当前有效版本 (哈希表中的offset匹配) */
            uint32_t hash_offset;
            if (kv_hash_get(&g_hash_table, record.key, record.key_len,
                           &hash_offset) == 0 && hash_offset == offset) {
                int ret = callback(record.key, record.key_len,
                                  record.value, record.value_len, user_data);
                if (ret != 0) {
                    return KV_OK;  /* 用户请求终止遍历 */
                }
            }
        }

        offset += sizeof(kv_record_t);
    }

    return KV_OK;
}

int flash_kv_clear(void)
{
    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    /* 擦除当前活跃区域 */
    if (handle->ops->erase(handle->region_addr[handle->active_region],
                          handle->region_size) != 0) {
        return KV_ERR_FLASH_FAIL;
    }

    /* 重置内存状态 */
    kv_hash_init(&g_hash_table);
    handle->record_count = 0;
    handle->active_offset = sizeof(kv_region_header_t);
    handle->version++;

    /* 写入新的区域头部 */
    return kv_region_header_write(handle, handle->active_region);
}

uint32_t flash_kv_count(void)
{
    return g_handles[0].record_count;
}

int flash_kv_status(uint32_t *total, uint32_t *used)
{
    kv_handle_t *handle = &g_handles[0];
    if (total == NULL || used == NULL) {
        return KV_ERR_INVALID_PARAM;
    }
    *total = handle->region_size - handle->block_size -
             sizeof(kv_region_header_t);
    *used = handle->active_offset - sizeof(kv_region_header_t);
    return KV_OK;
}
