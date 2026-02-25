# Flash KV 参数存储模块实现计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 实现一个基于MCU片内Flash的Key-Value参数存储模块，支持平台无关、日志式存储、双备份、原子性更新、哈希表索引、GC等功能。

**Architecture:** 采用分层架构：应用层 → KV核心层 → Flash适配层 → 平台抽象层。核心逻辑在src/flash_kv_core.c中实现，Flash操作通过适配层抽象，支持Linux内存模拟测试。

**Tech Stack:** C语言、CMake构建、Linux单元测试

---

## 实现顺序

1. 先实现测试框架和Mock Flash驱动
2. 实现基础数据结构（头文件）
3. 实现核心逻辑
4. 实现类型转换辅助函数
5. 实现Linux适配器
6. 运行测试验证

---

## Task 1: 创建项目目录结构和基础配置文件

**Files:**
- Create: `flash_kv/inc/flash_kv_config.h`
- Create: `flash_kv/inc/flash_kv_types.h`

**Step 1: 创建目录结构**

```bash
mkdir -p flash_kv/{inc,src,test,adapter}
```

**Step 2: 创建 flash_kv_config.h**

```c
#ifndef FLASH_KV_CONFIG_H
#define FLASH_KV_CONFIG_H

/*============================================================================
 * Flash 硬件配置 - 根据具体MCU修改
 *============================================================================*/

/* Flash 块大小 (擦除最小单位) 单位：字节 */
#define FLASH_KV_BLOCK_SIZE         2048

/* Flash 写入单位 (字节对齐要求) */
#define FLASH_KV_WRITE_SIZE        8

/* Flash 读取单位 */
#define FLASH_KV_READ_SIZE         1

/*============================================================================
 * KV 存储配置
 *============================================================================*/

/* Key 长度 (字节) */
#define FLASH_KV_KEY_SIZE          32

/* Value 长度 (字节) */
#define FLASH_KV_VALUE_SIZE        64

/* 每条记录总大小 = Key + Value + Meta(4) + CRC16(2) */
#define FLASH_KV_RECORD_SIZE       (FLASH_KV_KEY_SIZE + FLASH_KV_VALUE_SIZE + 4 + 2)

/* 最大记录条数 */
#define FLASH_KV_MAX_RECORDS       512

/*============================================================================
 * Flash 空间配置
 *============================================================================*/

/* Flash 起始地址 */
#define FLASH_KV_START_ADDR        0x0800F000

/* Flash 总大小 (字节) */
#define FLASH_KV_TOTAL_SIZE        (32 * 1024)

/*============================================================================
 * GC 配置
 *============================================================================*/

/* GC 触发阈值：空闲空间小于此值时触发 (百分比 0-100) */
#define FLASH_KV_GC_THRESHOLD     20

/*============================================================================
 * 哈希表配置
 *============================================================================*/

/* 哈希表大小 (必须是2的幂) */
#define FLASH_KV_HASH_SIZE        1024

/*============================================================================
 * 线程安全配置
 *============================================================================*/
#define FLASH_KV_THREAD_SAFE       0

/*============================================================================
 * 写入重试配置
 *============================================================================*/
#define FLASH_KV_WRITE_RETRY_MAX   3
#define FLASH_KV_WRITE_RETRY_DELAY_MS  10

/*============================================================================
 * 多实例支持
 *============================================================================*/
#define FLASH_KV_INSTANCE_MAX      2

#endif /* FLASH_KV_CONFIG_H */
```

**Step 3: 创建 flash_kv_types.h**

```c
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
```

**Step 4: Commit**

```bash
git add flash_kv/inc/flash_kv_config.h flash_kv/inc/flash_kv_types.h
git commit -m "feat: 添加配置文件 flash_kv_config.h 和类型定义 flash_kv_types.h"
```

---

## Task 2: 创建Mock Flash驱动（测试基础）

**Files:**
- Create: `flash_kv/test/mock_flash.c`

**Step 1: 创建 mock_flash.c**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "flash_kv_config.h"
#include "flash_kv_types.h"

typedef struct {
    uint8_t *memory;
    uint32_t size;
    uint32_t block_size;
} mem_flash_t;

static mem_flash_t g_flash = {0};

static int mem_flash_init(void)
{
    g_flash.size = 64 * 1024;  /* 64KB for testing */
    g_flash.block_size = FLASH_KV_BLOCK_SIZE;
    g_flash.memory = calloc(g_flash.size, 1);
    if (g_flash.memory == NULL) {
        return -1;
    }
    return 0;
}

static int mem_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr + len > g_flash.size) {
        return -1;
    }
    memcpy(buf, g_flash.memory + addr, len);
    return 0;
}

static int mem_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (addr + len > g_flash.size) {
        return -1;
    }
    /* 模拟Flash写入: 只能将1写成0 */
    for (uint32_t i = 0; i < len; i++) {
        g_flash.memory[addr + i] &= buf[i];
    }
    return 0;
}

static int mem_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t block_start = addr / g_flash.block_size;
    uint32_t block_end = (addr + len + g_flash.block_size - 1) / g_flash.block_size;

    for (uint32_t i = block_start; i < block_end; i++) {
        memset(g_flash.memory + i * g_flash.block_size, 0xFF, g_flash.block_size);
    }
    return 0;
}

const flash_kv_ops_t mock_flash_ops = {
    .init   = mem_flash_init,
    .read   = mem_flash_read,
    .write  = mem_flash_write,
    .erase  = mem_flash_erase,
};
```

**Step 2: Commit**

```bash
git add flash_kv/test/mock_flash.c
git commit -m "test: 添加Mock Flash驱动用于测试"
```

---

## Task 3: 实现CRC计算函数

**Files:**
- Create: `flash_kv/src/flash_kv_crc.c`

**Step 1: 创建 flash_kv_crc.c**

```c
#include "flash_kv_types.h"

/* CRC-16-CCITT 计算 */
uint16_t kv_crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

/* CRC-32 计算 */
uint32_t kv_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ 0x04C11DB7;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}
```

**Step 2: 创建头文件声明 flash_kv_crc.h**

```c
#ifndef FLASH_KV_CRC_H
#define FLASH_KV_CRC_H

#include <stdint.h>

uint16_t kv_crc16(const uint8_t *data, uint32_t len);
uint32_t kv_crc32(const uint8_t *data, uint32_t len);

#endif
```

**Step 3: Commit**

```bash
git add flash_kv/src/flash_kv_crc.c flash_kv/src/flash_kv_crc.h
git commit -m "feat: 实现CRC16和CRC32计算函数"
```

---

## Task 4: 实现哈希表

**Files:**
- Create: `flash_kv/src/flash_kv_hash.c`

**Step 1: 创建 flash_kv_hash.c**

```c
#include <string.h>
#include "flash_kv_types.h"

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
            table->count--;
            return 0;
        }
    }
    return -1;
}
```

**Step 2: 创建头文件 flash_kv_hash.h**

```c
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
```

**Step 3: Commit**

```bash
git add flash_kv/src/flash_kv_hash.c flash_kv/src/flash_kv_hash.h
git commit -m "feat: 实现哈希表（查找、插入、删除）"
```

---

## Task 5: 实现核心KV操作

**Files:**
- Create: `flash_kv/src/flash_kv_core.c`

**Step 1: 创建 flash_kv_core.c (基础框架)**

```c
#include <string.h>
#include "flash_kv_types.h"
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
    handle->start_addr = config->start_addr;
    handle->region_size = config->total_size / 2;
    handle->block_size = config->block_size;
    handle->region_addr[0] = config->start_addr;
    handle->region_addr[1] = config->start_addr + handle->region_size;

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

    kv_handle_t *handle = &g_handles[0];  /* 简化：使用instance 0 */
    if (handle->ops == NULL) {
        return KV_ERR_NO_INIT;
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
    memcpy(value, record.value, FLASH_KV_VALUE_SIZE);
    *value_len = FLASH_KV_VALUE_SIZE;

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

    /* 从哈希表删除 */
    if (kv_hash_del(&g_hash_table, key, key_len) != 0) {
        return KV_ERR_NOT_FOUND;
    }

    return KV_OK;
}

/* KV是否存在 */
bool flash_kv_exists(const uint8_t *key, uint8_t key_len)
{
    uint32_t offset;
    return (kv_hash_get(&g_hash_table, key, key_len, &offset) == 0);
}
```

**Step 2: 创建主头文件 flash_kv.h**

```c
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
```

**Step 3: Commit**

```bash
git add flash_kv/src/flash_kv_core.c flash_kv/inc/flash_kv.h
git commit -m "feat: 实现核心KV操作（set/get/del/exists）"
```

---

## Task 6: 实现类型转换辅助函数

**Files:**
- Create: `flash_kv/src/flash_kv_utils.c`

**Step 1: 创建 flash_kv_utils.c**

```c
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "flash_kv_types.h"

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
```

**Step 2: 创建头文件 flash_kv_utils.h**

```c
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
```

**Step 3: Commit**

```bash
git add flash_kv/src/flash_kv_utils.c flash_kv/src/flash_kv_utils.h
git commit -m "feat: 实现类型转换辅助函数"
```

---

## Task 7: 创建测试文件

**Files:**
- Create: `flash_kv/test/flash_kv_test.c`

**Step 1: 创建 flash_kv_test.c**

```c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "flash_kv.h"

extern const flash_kv_ops_t mock_flash_ops;

void test_kv_set_get(void)
{
    printf("Test: KV set/get... ");

    /* 初始化 */
    flash_kv_adapter_register(&mock_flash_ops);

    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    int ret = flash_kv_init(0, &config);
    assert(ret == KV_OK);

    /* 写入 */
    const uint8_t key[] = "test_key";
    const uint8_t value[] = "test_value_12345";
    ret = flash_kv_set(key, sizeof(key) - 1, value, sizeof(value) - 1);
    assert(ret == KV_OK);

    /* 读取 */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    assert(ret == KV_OK);
    assert(len == sizeof(value) - 1);
    assert(memcmp(read_val, value, sizeof(value) - 1) == 0);

    /* 存在检查 */
    assert(flash_kv_exists(key, sizeof(key) - 1) == true);

    printf("PASS\n");
}

void test_kv_update(void)
{
    printf("Test: KV update... ");

    /* 写入相同key的不同value */
    const uint8_t key[] = "update_key";
    const uint8_t value1[] = "value_version_1";
    const uint8_t value2[] = "value_version_2";

    flash_kv_set(key, sizeof(key) - 1, value1, sizeof(value1) - 1);
    flash_kv_set(key, sizeof(key) - 1, value2, sizeof(value2) - 1);

    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    int ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    assert(ret == KV_OK);
    assert(memcmp(read_val, value2, sizeof(value2) - 1) == 0);

    printf("PASS\n");
}

void test_kv_not_found(void)
{
    printf("Test: KV not found... ");

    const uint8_t key[] = "nonexistent_key";
    uint8_t value[64];
    uint8_t len = sizeof(value);

    int ret = flash_kv_get(key, sizeof(key) - 1, value, &len);
    assert(ret == KV_ERR_NOT_FOUND);

    printf("PASS\n");
}

void test_kv_type_utils(void)
{
    printf("Test: Type utils... ");

    uint8_t buf[4];

    /* uint16_t */
    kv_put_u16le(buf, 0x1234);
    assert(kv_get_u16le(buf) == 0x1234);

    /* uint32_t */
    kv_put_u32le(buf, 0x12345678);
    assert(kv_get_u32le(buf) == 0x12345678);

    /* float */
    kv_put_float(buf, 3.14159f);
    assert(kv_get_float(buf) > 3.14 && kv_get_float(buf) < 3.15);

    /* bool */
    kv_put_bool(buf, true);
    assert(kv_get_bool(buf) == true);
    kv_put_bool(buf, false);
    assert(kv_get_bool(buf) == false);

    printf("PASS\n");
}

int main(void)
{
    printf("=== Flash KV Unit Tests ===\n");

    test_kv_set_get();
    test_kv_update();
    test_kv_not_found();
    test_kv_type_utils();

    printf("\n=== All Tests PASSED ===\n");
    return 0;
}
```

**Step 2: Commit**

```bash
git add flash_kv/test/flash_kv_test.c
git commit -m "test: 添加单元测试"
```

---

## Task 8: 创建CMake构建文件

**Files:**
- Create: `flash_kv/CMakeLists.txt`

**Step 1: 创建 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.10)
project(flash_kv C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR})

# 源文件
set(SOURCES
    src/flash_kv_core.c
    src/flash_kv_crc.c
    src/flash_kv_hash.c
    src/flash_kv_utils.c
)

# 测试文件
set(TEST_SOURCES
    test/flash_kv_test.c
    test/mock_flash.c
)

# 库
add_library(flash_kv STATIC ${SOURCES})

# 测试可执行文件
add_executable(flash_kv_test ${TEST_SOURCES})
target_link_libraries(flash_kv_test flash_kv)
```

**Step 2: Commit**

```bash
git add flash_kv/CMakeLists.txt
git commit -m "build: 添加CMake构建文件"
```

---

## Task 9: 运行测试验证

**Step 1: 编译测试**

```bash
cd flash_kv
mkdir -p build && cd build
cmake .. && make
```

**Step 2: 运行测试**

```bash
./flash_kv_test
```

**预期输出:**
```
=== Flash KV Unit Tests ===
Test: KV set/get... PASS
Test: KV update... PASS
Test: KV not found... PASS
Test: Type utils... PASS

=== All Tests PASSED ===
```

**Step 3: Commit**

```bash
git add build/  # 可选
git commit -m "test: 运行测试验证通过"
```

---

## 总结

本计划包含以下任务：

| Task | 内容 |
|------|------|
| 1 | 创建目录结构和配置文件 |
| 2 | 创建Mock Flash驱动 |
| 3 | 实现CRC计算函数 |
| 4 | 实现哈希表 |
| 5 | 实现核心KV操作 |
| 6 | 实现类型转换辅助函数 |
| 7 | 创建单元测试 |
| 8 | 创建CMake构建文件 |
| 9 | 运行测试验证 |

每个任务都是独立的，可以按顺序实现和测试。
