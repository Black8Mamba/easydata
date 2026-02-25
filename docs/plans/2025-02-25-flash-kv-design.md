# Flash KV 参数存储模块设计方案

## 1. 模块架构和分层设计

### 1.1 整体架构

```
+--------------------------------------------------+
|                   应用层 (App)                   |
|          kv_set()/kv_get()/kv_del()             |
+--------------------------------------------------+
                     |
                     v
+--------------------------------------------------+
|               KV核心层 (KV Core)                 |
|   事务管理 | 日志管理 | GC | 双备份 | 索引缓存  |
+--------------------------------------------------+
                     |
                     v
+--------------------------------------------------+
|              Flash适配层 (Flash HAL)            |
|   flash_read() | flash_write() | flash_erase() |
+--------------------------------------------------+
                     |
                     v
+--------------------------------------------------+
|              平台抽象层 (Platform)               |
|        MCU具体Flash驱动 (用户实现)              |
+--------------------------------------------------+
```

### 1.2 分层说明

| 层次 | 职责 | 可替换性 |
|------|------|----------|
| 应用层 | 提供简洁的KV API | 固定 |
| KV核心层 | 核心逻辑：事务、日志、GC、双备份、索引 | 固定 |
| Flash适配层 | 统一不同MCU的Flash操作接口 | 固定接口，用户实现 |
| 平台抽象层 | 具体的Flash驱动实现 | 用户实现 |

### 1.3 目录结构设计

```
flash_kv/
├── inc/
│   ├── flash_kv.h          # 主头文件，对外API
│   ├── flash_kv_config.h  # 配置文件
│   ├── flash_kv_types.h   # 类型定义
│   └── flash_kv_adapter.h # Flash适配器接口
├── src/
│   ├── flash_kv_core.c    # 核心实现
│   ├── flash_kv_adapter.c # 适配层默认实现(可选)
│   └── flash_kv_utils.c   # 类型转换辅助函数
└── doc/
    └── flash_kv_design.md # 设计文档
```

---

## 2. Flash布局和双备份机制

### 2.1 Flash空间布局

假设配置：Flash总大小 64KB，块大小 2KB

```
+--------------------------------+ 0x0000 (起始地址)
|         引导区/其他用途         |
+--------------------------------+
|                                |
|     A区 (32KB)                 |  双备份 A
|     日志区域                   |
|                                |
+--------------------------------+
|                                |
|     B区 (32KB)                 |  双备份 B
|     日志区域                   |
|                                |
+--------------------------------+ 0x8000 (结束)
```

### 2.2 单区内部布局

每区包含：头部信息 + 日志数据区

```
+--------------------------------+
|  Header (1块)                  |  2KB
|  - magic number               |
|  - 版本号                      |
|  - 记录计数                    |
|  - CRC32                      |
+--------------------------------+
|                                |
|  日志数据区                    |  30KB
|  [Record 1] [Record 2] ...    |
|  追加写入                      |
|                                |
+--------------------------------+
```

### 2.3 记录格式 (Record)

```
+------------------+------------------+------------------+------------+
| Key (32B)        | Value (64B)      | Meta (4B)        | CRC16 (2B) |
+------------------+------------------+------------------+------------+
```

- **Key**: 固定长度，不足补0x00
- **Value**: 固定长度，不足补0x00
- **Meta**: 包含 flags(1B) + reserved(3B)
- **CRC16**: 每条记录的校验和

### 2.4 双备份机制

```
状态机：
  [初始化] --> [检测有效区] --> [选择活跃区]
                                    |
                                    v
  [读取/写入] <---- [A区活跃]   [B区活跃]
      |              ^                ^
      |              |                |
      +----切换-------+----切换-------+
```

**切换逻辑**:
1. 上电时读取A区和B区的头部
2. 比较版本号，选择版本号大的作为活跃区
3. 如果版本号相同，选择CRC校验通过的区
4. 如果都无效，使用A区并格式化

---

## 3. 原子性实现思路

### 3.1 事务机制设计

采用 **"预留区 + 原子提交"** 策略：

```
Step 1: 写入预留区 (Temp Area)
        +--------------------------------+
        |  A区           |  预留区       |
        |  ...old...    |  NEW_RECORD   |
        +--------------------------------+

Step 2: 验证写入
        - 读取预留区数据
        - 验证CRC正确性

Step 3: 原子提交 (更新头部版本号)
        - 递增版本号
        - 更新CRC
        - 一次性写入头部

Step 4: 清理旧数据 (可选，由GC处理)
```

### 3.2 事务状态机

```c
typedef enum {
    KV_TX_STATE_IDLE,       // 空闲
    KV_TX_STATE_PREPARED,  // 已写入预留区
    KV_TX_STATE_COMMITTED, // 已提交
    KV_TX_STATE_ROLLBACK   // 回滚中
} kv_tx_state_t;
```

### 3.3 断电恢复

- 事务状态保存在Flash头部
- 上电检测：如果头部版本号 > 内存中的版本号，说明有未完成的事务
- 恢复策略：
  1. 读取活跃区最新记录
  2. 跳过损坏的记录（CRC校验失败）
  3. 使用最新的有效数据

---

## 4. 数据结构设计

### 4.1 配置文件 (flash_kv_config.h)

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

/* 每条记录总大小 = Key + Value + Meta(4) + CRC16(2) = 102 */
#define FLASH_KV_RECORD_SIZE       (FLASH_KV_KEY_SIZE + FLASH_KV_VALUE_SIZE + 4 + 2)

/* 最大记录条数 */
#define FLASH_KV_MAX_RECORDS       512

/*============================================================================
 * Flash 空间配置
 *============================================================================*/

/* Flash 起始地址 */
#define FLASH_KV_START_ADDR        0x0800F000

/* Flash 总大小 (字节) */
#define FLASH_KV_TOTAL_SIZE        (32 * 1024)  /* 32KB per region */

/* 预留区大小 */
#define FLASH_KV_RESERVE_SIZE      FLASH_KV_BLOCK_SIZE

/*============================================================================
 * GC 配置
 *============================================================================*/

/* GC 触发阈值：空闲空间小于此值时触发 (百分比 0-100) */
#define FLASH_KV_GC_THRESHOLD     20

/*============================================================================
 * 多实例支持
 *============================================================================*/
#define FLASH_KV_INSTANCE_MAX      2

#endif /* FLASH_KV_CONFIG_H */
```

### 4.2 类型定义 (flash_kv_types.h)

```c
#ifndef FLASH_KV_TYPES_H
#define FLASH_KV_TYPES_H

#include <stdint.h>
#include <stdbool.h>

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
    KV_ERR_INVALID_REGION = -9
} kv_err_t;

/*============================================================================
 * 事务状态
 *============================================================================*/
typedef enum {
    KV_TX_IDLE = 0,
    KV_TX_PREPARED,
    KV_TX_COMMITTED
} kv_tx_state_t;

/*============================================================================
 * Flash 操作接口 (用户实现)
 *============================================================================*/
typedef struct {
    /* 初始化 */
    int (*init)(void);

    /* 读取 */
    int (*read)(uint32_t addr, uint8_t *buf, uint32_t len);

    /* 写入 (写入前需确保已擦除) */
    int (*write)(uint32_t addr, const uint8_t *buf, uint32_t len);

    /* 擦除 (按块擦除) */
    int (*erase)(uint32_t addr, uint32_t len);
} flash_kv_ops_t;

/*============================================================================
 * KV 实例配置
 *============================================================================*/
typedef struct {
    uint32_t start_addr;      /* 起始地址 */
    uint32_t total_size;      /* 总大小 */
    uint32_t block_size;     /* 块大小 */
    const flash_kv_ops_t *ops; /* Flash操作接口 */
} kv_instance_config_t;

/*============================================================================
 * KV 句柄
 *============================================================================*/
typedef struct kv_handle {
    uint8_t instance_id;
    uint32_t active_region;    /* 当前活跃区域: 0=A, 1=B */
    uint32_t version;         /* 当前版本号 */
    uint32_t record_count;    /* 有效记录数 */
    kv_tx_state_t tx_state;   /* 事务状态 */
    void *index_cache;        /* 索引缓存 */
} kv_handle_t;

/*============================================================================
 * 记录结构
 *============================================================================*/
typedef struct {
    uint8_t key[FLASH_KV_KEY_SIZE];
    uint8_t value[FLASH_KV_VALUE_SIZE];
    uint8_t flags;            /* 标志位 */
    uint8_t reserved[3];     /* 保留 */
    uint16_t crc16;          /* CRC校验 */
} __attribute__((packed)) kv_record_t;

/*============================================================================
 * 区域头部
 *============================================================================*/
typedef struct {
    uint32_t magic;           /* 魔术字: 0x4B5653xx */
    uint32_t version;         /* 版本号 */
    uint32_t record_count;    /* 有效记录数 */
    uint32_t active_offset;   /* 活跃数据结束偏移 */
    uint32_t crc32;           /* 头部CRC */
} __attribute__((packed)) kv_region_header_t;

#endif /* FLASH_KV_TYPES_H */
```

---

## 5. API接口设计

### 5.1 核心API (flash_kv.h)

```c
#ifndef FLASH_KV_H
#define FLASH_KV_H

#include "flash_kv_config.h"
#include "flash_kv_types.h"

/*============================================================================
 * 初始化与配置
 *============================================================================*/

/**
 * @brief 初始化 KV 存储模块
 * @param instance_id 实例ID (0-1)
 * @param config 实例配置
 * @return KV_OK 成功，其他失败
 */
int flash_kv_init(uint8_t instance_id, const kv_instance_config_t *config);

/**
 * @brief 获取KV句柄
 * @param instance_id 实例ID
 * @return KV句柄，NULL表示无效
 */
kv_handle_t* flash_kv_get_handle(uint8_t instance_id);

/**
 * @brief 去初始化
 * @param instance_id 实例ID
 * @return KV_OK 成功
 */
int flash_kv_deinit(uint8_t instance_id);

/*============================================================================
 * 基本操作
 *============================================================================*/

/**
 * @brief 设置参数
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return KV_OK 成功，其他失败
 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len);

/**
 * @brief 获取参数
 * @param key 键
 * @param key_len 键长度
 * @param value 输出缓冲区
 * @param value_len 输入输出：传入缓冲区长度，返回实际长度
 * @return KV_OK 成功，KV_ERR_NOT_FOUND 未找到
 */
int flash_kv_get(const uint8_t *key, uint8_t key_len,
                 uint8_t *value, uint8_t *value_len);

/**
 * @brief 删除参数
 * @param key 键
 * @param key_len 键长度
 * @return KV_OK 成功
 */
int flash_kv_del(const uint8_t *key, uint8_t key_len);

/**
 * @brief 检查键是否存在
 * @param key 键
 * @param key_len 键长度
 * @return true 存在，false 不存在
 */
bool flash_kv_exists(const uint8_t *key, uint8_t key_len);

/*============================================================================
 * 事务操作
 *============================================================================*/

/**
 * @brief 开始事务
 * @return KV_OK 成功，其他失败
 */
int flash_kv_tx_begin(void);

/**
 * @brief 提交事务
 * @return KV_OK 成功，其他失败
 */
int flash_kv_tx_commit(void);

/**
 * @brief 回滚事务
 * @return KV_OK 成功
 */
int flash_kv_tx_rollback(void);

/*============================================================================
 * GC 操作
 *============================================================================*/

/**
 * @brief 手动触发垃圾回收
 * @return KV_OK 成功，其他失败
 */
int flash_kv_gc(void);

/**
 * @brief 获取空闲空间百分比
 * @return 0-100
 */
uint8_t flash_kv_free_percent(void);

/*============================================================================
 * 批量操作
 *============================================================================*/

/**
 * @brief 遍历所有参数
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return KV_OK 成功
 */
typedef int (*kv_foreach_cb)(const uint8_t *key, uint8_t key_len,
                             const uint8_t *value, uint8_t value_len,
                             void *user_data);

int flash_kv_foreach(kv_foreach_cb callback, void *user_data);

/**
 * @brief 清空所有参数
 * @return KV_OK 成功
 */
int flash_kv_clear(void);

/*============================================================================
 * 状态查询
 *============================================================================*/

/**
 * @brief 获取记录总数
 * @return 记录数
 */
uint32_t flash_kv_count(void);

/**
 * @brief 获取存储状态
 * @param total 总空间
 * @param used 已使用空间
 * @return KV_OK 成功
 */
int flash_kv_status(uint32_t *total, uint32_t *used);

#endif /* FLASH_KV_H */
```

### 5.2 类型转换辅助函数 (flash_kv_utils.h)

```c
#ifndef FLASH_KV_UTILS_H
#define FLASH_KV_UTILS_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * 整数类型转换
 *============================================================================*/

/* uint8_t */
void kv_put_u8(uint8_t *buf, uint8_t val);
uint8_t kv_get_u8(const uint8_t *buf);

/* int8_t */
void kv_put_i8(uint8_t *buf, int8_t val);
int8_t kv_get_i8(const uint8_t *buf);

/* uint16_t (大端) */
void kv_put_u16be(uint8_t *buf, uint16_t val);
uint16_t kv_get_u16be(const uint8_t *buf);

/* uint16_t (小端) */
void kv_put_u16le(uint8_t *buf, uint16_t val);
uint16_t kv_get_u16le(const uint8_t *buf);

/* uint32_t (大端) */
void kv_put_u32be(uint8_t *buf, uint32_t val);
uint32_t kv_get_u32be(const uint8_t *buf);

/* uint32_t (小端) */
void kv_put_u32le(uint8_t *buf, uint32_t val);
uint32_t kv_get_u32le(const uint8_t *buf);

/*============================================================================
 * 浮点类型转换
 *============================================================================*/

/* float */
void kv_put_float(uint8_t *buf, float val);
float kv_get_float(const uint8_t *buf);

/* double */
void kv_put_double(uint8_t *buf, double val);
double kv_get_double(const uint8_t *buf);

/*============================================================================
 * 字符串操作
 *============================================================================*/

/**
 * @brief 格式化字符串写入缓冲区
 * @param buf 缓冲区
 * @param buf_size 缓冲区大小
 * @param fmt 格式字符串
 * @return 写入的字节数
 */
int kv_snprintf(uint8_t *buf, uint8_t buf_size, const char *fmt, ...);

/**
 * @brief 从缓冲区读取字符串
 * @param buf 缓冲区
 * @param buf_len 缓冲区长度
 * @param str 输出字符串
 * @param str_size 输出字符串大小
 * @return 0 成功
 */
int kv_sscanf(const uint8_t *buf, uint8_t buf_len,
              char *str, uint8_t str_size);

/*============================================================================
 * 布尔类型
 *============================================================================*/
void kv_put_bool(uint8_t *buf, bool val);
bool kv_get_bool(const uint8_t *buf);

#endif /* FLASH_KV_UTILS_H */
```

### 5.3 Flash适配器接口 (flash_kv_adapter.h)

```c
#ifndef FLASH_KV_ADAPTER_H
#define FLASH_KV_ADAPTER_H

#include "flash_kv_types.h"

/*============================================================================
 * Flash 适配器注册
 *============================================================================*/

/**
 * @brief 注册Flash操作接口
 * @param ops Flash操作接口
 * @return KV_OK 成功
 */
int flash_kv_adapter_register(const flash_kv_ops_t *ops);

/**
 * @brief 获取当前Flash操作接口
 * @return Flash操作接口
 */
const flash_kv_ops_t* flash_kv_adapter_get(void);

/*============================================================================
 * 默认空实现 (可选)
 *============================================================================*/
extern const flash_kv_ops_t flash_kv_default_adapter;

#endif /* FLASH_KV_ADAPTER_H */
```

---

## 6. 核心实现思路

### 6.1 哈希表索引机制

采用**开放地址法哈希表**，提高查询效率：

#### 哈希表配置

```c
/* 哈希表大小 (必须是2的幂，便于取模) */
#define FLASH_KV_HASH_SIZE      512

/* 哈希函数: DJB2 */
#define KV_HASH(key, len)      kv_hash_djb2(key, len)

static inline uint16_t kv_hash_djb2(const uint8_t *key, uint8_t len) {
    uint32_t hash = 5381;
    for (uint8_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + key[i];  /* hash * 33 + key[i] */
    }
    return hash & (FLASH_KV_HASH_SIZE - 1);
}
```

#### 哈希表结构

```c
typedef struct {
    uint8_t  key_len;          /* Key长度，0表示空槽 */
    uint8_t  key[FLASH_KV_KEY_SIZE];  /* Key内容 */
    uint32_t flash_offset;     /* 在Flash中的偏移 */
} kv_hash_slot_t;

typedef struct {
    kv_hash_slot_t slots[FLASH_KV_HASH_SIZE];
    uint16_t count;            /* 已存储的键值对数量 */
} kv_hash_table_t;
```

#### 索引操作

**查找 (O(1) 平均)**:
```c
int kv_hash_get(const uint8_t *key, uint8_t key_len, uint32_t *offset) {
    uint16_t hash = KV_HASH(key, key_len);

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);  /* 线性探测 */
        kv_hash_slot_t *slot = &hash_table->slots[idx];

        if (slot->key_len == 0) {
            return -1;  /* 未找到 */
        }

        if (slot->key_len == key_len &&
            memcmp(slot->key, key, key_len) == 0) {
            *offset = slot->flash_offset;
            return 0;  /* 找到 */
        }
    }
    return -1;  /* 未找到 */
}
```

**插入/更新 (O(1) 平均)**:
```c
int kv_hash_set(const uint8_t *key, uint8_t key_len, uint32_t offset) {
    uint16_t hash = KV_HASH(key, key_len);

    for (int i = 0; i < FLASH_KV_HASH_SIZE; i++) {
        uint16_t idx = (hash + i) & (FLASH_KV_HASH_SIZE - 1);
        kv_hash_slot_t *slot = &hash_table->slots[idx];

        /* 空槽或Key相同 -> 插入/更新 */
        if (slot->key_len == 0 || (slot->key_len == key_len &&
            memcmp(slot->key, key, key_len) == 0)) {
            slot->key_len = key_len;
            memcpy(slot->key, key, key_len);
            slot->flash_offset = offset;
            return 0;
        }
    }

    return -1;  /* 哈希表已满 */
}
```

#### 哈希表重建

GC完成后需要重建哈希表：

```c
void kv_hash_rebuild(void) {
    memset(hash_table, 0, sizeof(kv_hash_table_t));

    /* 遍历Flash，重新构建哈希表 */
    uint32_t offset = sizeof(kv_region_header_t);
    while (offset < region->active_offset) {
        kv_record_t record;
        flash_read(region_addr + offset, &record, sizeof(record));

        if (kv_record_valid(&record)) {
            kv_hash_set(record.key, FLASH_KV_KEY_SIZE, offset);
        }
        offset += FLASH_KV_RECORD_SIZE;
    }
}
```

#### 内存占用

| 项目 | 大小 |
|------|------|
| 哈希表槽数 | 512 |
| 每槽大小 | 1 + 32 + 4 = 37 字节 |
| 总内存 | 约 19 KB |

> 注：哈希表大小可根据实际条目数量调整，建议设置为预期最大条目的 1.5-2 倍。

### 6.2 GC策略

```c
typedef enum {
    GC_TYPE_PASSIVE,   // 被动触发：空间不足
    GC_TYPE_MANUAL,    // 手动触发
    GC_TYPE_COMPACT   // 整理模式
} gc_type_t;
```

**被动GC流程**:
```
写入前检查:
  if (free_space < GC_THRESHOLD) {
      gc();
  }

gc():
  1. 扫描活跃区所有有效记录
  2. 构建有效记录列表
  3. 切换到备用区
  4. 将有效记录全部写入备用区
  5. 更新头部版本号(原子操作)
  6. 擦除旧区
```

### 6.3 双备份切换

```c
int kv_switch_region(void) {
    uint32_t new_region = (handle->active_region == 0) ? 1 : 0;

    // 原子操作：只更新头部
    kv_region_header_t header;
    header.magic = KV_MAGIC;
    header.version = handle->version + 1;
    header.record_count = 0;
    header.active_offset = sizeof(kv_region_header_t);
    header.crc32 = crc32(&header, offsetof(kv_region_header_t, crc32));

    // 写入新区头部
    flash_write(region_addr[new_region], &header, sizeof(header));

    handle->active_region = new_region;
    return KV_OK;
}
```

---

## 7. 使用示例

```c
/* 用户实现 Flash 驱动 */
static const flash_kv_ops_t my_flash_ops = {
    .init = my_flash_init,
    .read = my_flash_read,
    .write = my_flash_write,
    .erase = my_flash_erase,
};

/* 初始化 */
void app_init(void) {
    kv_instance_config_t config = {
        .start_addr = 0x0800F000,
        .total_size = 32 * 1024,
        .block_size = 2048,
        .ops = &my_flash_ops,
    };

    flash_kv_adapter_register(&my_flash_ops);
    flash_kv_init(0, &config);
}

/* 使用示例 */
void app_example(void) {
    /* 设置参数 */
    uint8_t value[64];
    uint8_t len = sizeof(value);

    /* 写入 */
    flash_kv_set((uint8_t*)"device_name", 11, (uint8_t*)"SmartSensor", 12);

    /* 读取 */
    if (flash_kv_get((uint8_t*)"device_name", 11, value, &len) == KV_OK) {
        printf("Device: %s\n", value);
    }

    /* 事务操作 */
    flash_kv_tx_begin();
    flash_kv_set((uint8_t*)"ip_addr", 7, (uint8_t*)"192.168.1.100", 14);
    flash_kv_set((uint8_t*)"port", 4, (uint8_t*)"8080", 4);
    flash_kv_tx_commit();

    /* 手动GC */
    if (flash_kv_free_percent() < 20) {
        flash_kv_gc();
    }
}
```

---

## 8. 关键技术点总结

| 需求 | 实现方案 |
|------|----------|
| 平台无关 | Flash适配层，定义统一接口，用户实现具体驱动 |
| 固定长度KV | 配置化 Key/Value 长度，默认32/64字节 |
| 日志式 | 追加写入，更新操作变成新增记录 |
| 索引 | 哈希表 (开放地址法)，O(1) 查找复杂度 |
| GC | 被动+手动，扫描有效记录并复制到新区 |
| 双备份 | A/B区交替使用，通过版本号选择活跃区 |
| 原子性 | 预留区写入 + 版本号原子更新 |
| 事务 | begin/commit/rollback 三阶段 |
| 多实例 | 实例ID + 句柄数组支持 |
| 类型转换 | 辅助函数库，支持常见类型 |
