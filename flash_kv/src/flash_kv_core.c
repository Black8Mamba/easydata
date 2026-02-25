#include <string.h>
#include <stdio.h>
#include "flash_kv.h"
#include "flash_kv_hash.h"
#include "flash_kv_crc.h"

/* 全局句柄 */
static kv_handle_t g_handles[FLASH_KV_INSTANCE_MAX];
static kv_hash_table_t g_hash_table;
static const flash_kv_ops_t *g_flash_ops = NULL;
static uint8_t g_initialized = 0;

/* 初始化Flash适配器 */
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

/* 初始化KV存储 */
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

    /* 初始化哈希表 */
    kv_hash_init(&g_hash_table);

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

/* 验证记录CRC */
static int kv_record_check_crc(const kv_record_t *record)
{
    uint16_t crc = kv_crc16((const uint8_t *)record,
                            sizeof(kv_record_t) - 2);
    return (crc == record->crc16) ? 0 : -1;
}

/* 写入记录 */
static int kv_record_write(kv_handle_t *handle, uint32_t offset,
                          const kv_record_t *record)
{
    kv_record_t r = *record;
    r.crc16 = kv_crc16((const uint8_t *)&r,
                       sizeof(kv_record_t) - 2);
    return handle->ops->write(offset, (const uint8_t *)&r, sizeof(r));
}

/* KV设置 */
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

    /* 检查key是否已存在，如存在则标记旧记录为已删除 */
    uint32_t old_offset;
    if (kv_hash_get(&g_hash_table, key, key_len, &old_offset) == 0) {
        /* 读取旧记录并标记为已删除 */
        uint32_t region_addr = handle->region_addr[handle->active_region];
        kv_record_t old_record;
        if (handle->ops->read(region_addr + old_offset, (uint8_t *)&old_record,
                             sizeof(old_record)) == 0) {
            old_record.flags = 2;  /* DELETED */
            old_record.crc16 = kv_crc16((const uint8_t *)&old_record,
                                        sizeof(kv_record_t) - 2);
            handle->ops->write(region_addr + old_offset, (const uint8_t *)&old_record,
                             sizeof(old_record));
        }
    }

    /* 检查空间 */
    uint32_t region_addr = handle->region_addr[handle->active_region];
    uint32_t reserve_offset = region_addr + handle->region_size - handle->block_size;
    uint32_t write_offset = region_addr + sizeof(kv_region_header_t) +
                            handle->record_count * sizeof(kv_record_t);

    if (write_offset >= reserve_offset) {
        return KV_ERR_NO_SPACE;
    }

    /* 构造记录 */
    kv_record_t record;
    memset(&record, 0, sizeof(record));
    memcpy(record.key, key, key_len);
    memcpy(record.value, value, value_len);
    record.value_len = value_len;
    record.flags = 1;  /* VALID */

    /* 写入 */
    int ret = kv_record_write(handle, write_offset, &record);
    if (ret != 0) {
        return KV_ERR_FLASH_FAIL;
    }

    /* 更新哈希表 */
    kv_hash_set(&g_hash_table, key, key_len,
                write_offset - region_addr);

    handle->record_count++;
    return KV_OK;
}

/* KV获取 */
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
    memcpy(value, record.value, record.value_len);
    *value_len = record.value_len;

    return KV_OK;
}

/* KV删除 */
int flash_kv_del(const uint8_t *key, uint8_t key_len)
{
    if (key == NULL) {
        return KV_ERR_INVALID_PARAM;
    }

    kv_handle_t *handle = &g_handles[0];
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
    }

    /* 先获取Flash中的偏移量，然后从哈希表删除 */
    uint32_t offset;
    if (kv_hash_get(&g_hash_table, key, key_len, &offset) != 0) {
        return KV_ERR_NOT_FOUND;
    }

    /* 读取旧记录并标记为已删除 */
    uint32_t region_addr = handle->region_addr[handle->active_region];
    kv_record_t record;
    if (handle->ops->read(region_addr + offset, (uint8_t *)&record,
                         sizeof(record)) == 0) {
        /* 标记为已删除 */
        record.flags = 2;  /* DELETED */
        record.crc16 = kv_crc16((const uint8_t *)&record,
                                sizeof(kv_record_t) - 2);
        handle->ops->write(region_addr + offset, (const uint8_t *)&record,
                         sizeof(record));
    }

    /* 从哈希表删除 */
    kv_hash_del(&g_hash_table, key, key_len);
    handle->record_count--;

    return KV_OK;
}

/* KV是否存在 */
bool flash_kv_exists(const uint8_t *key, uint8_t key_len)
{
    uint32_t offset;
    return (kv_hash_get(&g_hash_table, key, key_len, &offset) == 0);
}

/* 事务接口 - 简化实现 */
int flash_kv_tx_begin(void)
{
    return KV_OK;
}

int flash_kv_tx_commit(void)
{
    return KV_OK;
}

int flash_kv_tx_rollback(void)
{
    return KV_OK;
}

/* GC接口 - 简化实现 */
int flash_kv_gc(void)
{
    return KV_ERR_GC_FAIL;  /* 未实现 */
}

uint8_t flash_kv_free_percent(void)
{
    kv_handle_t *handle = &g_handles[0];
    uint32_t used = handle->record_count * sizeof(kv_record_t);
    uint32_t total = handle->region_size - handle->block_size - sizeof(kv_region_header_t);
    if (total == 0) return 0;
    return (uint8_t)((total - used) * 100 / total);
}

/* 批量操作接口 - 简化实现 */
int flash_kv_foreach(kv_foreach_cb callback, void *user_data)
{
    (void)callback;
    (void)user_data;
    return KV_ERR_NOT_FOUND;
}

int flash_kv_clear(void)
{
    kv_handle_t *handle = &g_handles[0];

    /* 擦除当前活跃区域 */
    if (handle->ops && handle->ops->erase) {
        handle->ops->erase(handle->region_addr[handle->active_region],
                          handle->region_size);
    }

    /* 清除内存中的哈希表和计数 */
    kv_hash_init(&g_hash_table);
    handle->record_count = 0;

    return KV_OK;
}

uint32_t flash_kv_count(void)
{
    return g_handles[0].record_count;
}

int flash_kv_status(uint32_t *total, uint32_t *used)
{
    kv_handle_t *handle = &g_handles[0];
    *total = handle->region_size - handle->block_size - sizeof(kv_region_header_t);
    *used = handle->record_count * sizeof(kv_record_t);
    return KV_OK;
}
