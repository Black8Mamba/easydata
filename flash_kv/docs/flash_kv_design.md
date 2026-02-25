# Flash KV 设计与实现文档

> 基于Flash的嵌入式Key-Value存储系统

## 目录

1. [项目概述](#1-项目概述)
2. [功能特性](#2-功能特性)
3. [系统架构](#3-系统架构)
4. [核心设计](#4-核心设计)
5. [数据结构](#5-数据结构)
6. [接口说明](#6-接口说明)
7. [核心流程](#7-核心流程)
8. [移植指南](#8-移植指南)
9. [调试与测试](#9-调试与测试)
10. [性能优化](#10-性能优化)
11. [常见问题](#11-常见问题)

---

## 1. 项目概述

### 1.1 什么是 Flash KV

Flash KV 是一个专为嵌入式MCU设计的轻量级Key-Value存储库，基于Flash掉电不失特性实现持久化存储。相比传统EEPROM，具有以下优势：

- **容量大**: 可使用整片Flash区域，容量从几KB到几MB
- **速度快**: 直接操作Flash，无需额外EEPROM芯片
- **易使用**: 简单API，类似Redis/NoSQL的键值存储体验
- **低功耗**: 无需额外硬件，STM32内部Flash即可

### 1.2 适用场景

```
┌─────────────────────────────────────────────────────────┐
│                    Flash KV 适用场景                      │
├─────────────────────────────────────────────────────────┤
│  • 设备配置参数存储 (WiFi SSID/密码、波特率)             │
│  • 用户偏好设置 (主题、语言、界面配置)                    │
│  • 运行时状态保存 (校准数据、计数器)                      │
│  • 固件升级参数 (版本号、升级标志)                       │
│  • 日志缓存 (故障记录、运行日志)                         │
└─────────────────────────────────────────────────────────┘
```

### 1.3 技术规格

| 参数 | 说明 |
|------|------|
| Key长度 | 最大32字节 |
| Value长度 | 最大64字节 |
| 最大记录数 | 512条 |
| Flash最小要求 | 32KB |
| 代码量 | ~1500行C代码 |
| RAM占用 | ~2KB |

---

## 2. 功能特性

### 2.1 核心功能

```
┌────────────────────────────────────────────────────────────┐
│                      Flash KV 功能矩阵                      │
├─────────────────────┬──────────────────────────────────────┤
│     功能类别        │              支持的操作                │
├─────────────────────┼──────────────────────────────────────┤
│   基本操作          │  set / get / del / exists            │
│   批量操作          │  foreach / count / clear             │
│   事务支持          │  begin / commit / rollback            │
│   垃圾回收          │  gc / free_percent                    │
│   状态查询          │  status / count                       │
└─────────────────────┴──────────────────────────────────────┘
```

### 2.2 高级特性

1. **双区域备份 (A/B Region)**
   - 两个Flash区域交替使用
   - 掉电安全：写操作失败不影响原数据
   - 自动故障恢复

2. **事务支持**
   - 批量操作原子性
   - 断电安全：事务要么全部成功，要么全部回滚

3. **垃圾回收 (GC)**
   - 手动/自动触发
   - 有效数据压缩整理
   - 阈值可配置

4. **数据完整性**
   - CRC-16校验：检测单bit错误
   - CRC-32校验：区域头完整性
   - 魔术字验证：识别有效区域

---

## 3. 系统架构

### 3.1 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                        应用层                                │
│  ┌─────────────────────────────────────────────────────────┐│
│  │  flash_kv_set() / get() / del() / exists()            ││
│  │  flash_kv_tx_begin() / commit() / rollback()           ││
│  │  flash_kv_gc() / foreach() / status()                   ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                       核心层                                │
│  ┌──────────────┬──────────────┬──────────────┐            │
│  │  kv_set()    │  kv_get()    │   kv_del()   │            │
│  ├──────────────┼──────────────┼──────────────┤            │
│  │  哈希索引    │   GC模块     │  事务管理    │            │
│  └──────────────┴──────────────┴──────────────┘            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      适配器层                                │
│  ┌─────────────────────────────────────────────────────────┐│
│  │         flash_kv_adapter_register(ops)                  ││
│  │                                                         ││
│  │    ┌───────────┐  ┌───────────┐  ┌───────────┐        ││
│  │    │  init()   │  │  read()   │  │  write()  │        ││
│  │    └───────────┘  └───────────┘  └───────────┘        ││
│  │    ┌───────────┐                                      ││
│  │    │  erase()  │   (用户实现的具体Flash驱动)          ││
│  │    └───────────┘                                      ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                       硬件层                                 │
│              STM32 / ESP32 / nRF52 等MCU                     │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 目录结构

```
flash_kv/
├── inc/                    # 公开头文件 (用户使用)
│   ├── flash_kv.h         # 主API头文件
│   ├── flash_kv_types.h   # 类型定义
│   └── flash_kv_config.h  # 配置文件
├── src/                    # 核心实现
│   ├── flash_kv_core.c    # 核心逻辑
│   ├── flash_kv_hash.c    # 哈希表
│   ├── flash_kv_crc.c     # CRC校验
│   └── flash_kv_utils.c   # 工具函数
├── test/                   # 单元测试
│   ├── flash_kv_test.c    # 测试用例
│   └── mock_flash.c       # 模拟Flash驱动
├── demo/
│   └── stm32/             # STM32示例
└── docs/
    └── flash_kv_design.md # 本文档
```

---

## 4. 核心设计

### 4.1 Flash布局

```
┌─────────────────────────────────────────────────────────────────┐
│                    双区域 A/B 备份布局                           │
├────────────────────────┬────────────────────────────────────────┤
│      Region A          │            Region B                   │
│  ┌──────────────────┐  │  ┌──────────────────┐                 │
│  │  Region Header   │  │  │  Region Header   │                 │
│  │  - Magic: KVSA   │  │  │  - Magic: KVSB   │                 │
│  │  - Version       │  │  │  - Version       │                 │
│  │  - Record Count  │  │  │  - Record Count │                 │
│  │  - Active Offset │  │  │  - Active Offset │                 │
│  │  - TX State      │  │  │  - TX State      │                 │
│  │  - CRC32         │  │  │  - CRC32         │                 │
│  └──────────────────┘  │  └──────────────────┘                 │
│  ┌──────────────────┐  │  ┌──────────────────┐                 │
│  │   KV Records     │  │  │   KV Records     │                 │
│  │  ┌────────────┐   │  │  │  ┌────────────┐   │                 │
│  │  │ Record 1   │   │  │  │  │ Record 1   │   │                 │
│  │  ├────────────┤   │  │  │  ├────────────┤   │                 │
│  │  │ Record 2   │   │  │  │  │ Record 2   │   │                 │
│  │  ├────────────┤   │  │  │  ├────────────┤   │                 │
│  │  │ Record 3   │   │  │  │  │ Record 3   │   │                 │
│  │  └────────────┘   │  │  │  └────────────┘   │                 │
│  └──────────────────┘  │  └──────────────────┘                 │
│                        │                                         │
└────────────────────────┴────────────────────────────────────────┘
```

### 4.2 记录格式

```
┌─────────────────────────────────────────────────────────────────┐
│                      单条KV记录结构                              │
├─────────────────────────────────────────────────────────────────┤
│  Offset  │  Field       │  Size   │  Description              │
├──────────┼───────────────┼─────────┼────────────────────────────┤
│    0     │  key          │  32B    │  Key数据 (不足补0)         │
│   32     │  value        │  64B    │  Value数据 (不足补0)      │
│   96     │  key_len      │   1B    │  实际Key长度              │
│   97     │  value_len    │   1B    │  实际Value长度           │
│   98     │  flags        │   1B    │  bit0:valid, bit1:del   │
│   99     │  reserved[1]  │   1B    │  保留                     │
│  100     │  crc16        │   2B    │  CRC-16校验               │
├──────────┼───────────────┼─────────┼────────────────────────────┤
│  Total   │               │ 102B    │  单条记录大小             │
└──────────┴───────────────┴─────────┴────────────────────────────┘
```

### 4.3 区域头部

```
┌─────────────────────────────────────────────────────────────────┐
│                      Region Header 结构                         │
├─────────────────────────────────────────────────────────────────┤
│  Offset  │  Field         │  Size   │  Description            │
├──────────┼────────────────┼─────────┼───────────────────────── │
│    0     │  magic         │   4B    │  魔术字 (KVSA/KVSB)     │
│    4     │  version       │   4B    │  版本号                 │
│    8     │  record_count  │   4B    │  有效记录数             │
│   12     │  active_offset │   4B    │  写入起始偏移           │
│   16     │  tx_state      │   1B    │  事务状态               │
│   17     │  reserved[3]   │   3B    │  保留                   │
│   20     │  crc32         │   4B    │  头部CRC-32校验         │
├──────────┼────────────────┼─────────┼───────────────────────── │
│  Total   │                │  24B    │  头部大小               │
└──────────┴────────────────┴─────────┴─────────────────────────────┘
```

### 4.4 哈希表设计

采用**开放寻址法 (Open Addressing)** 解决哈希冲突：

```
┌─────────────────────────────────────────────────────────────────┐
│                      哈希表结构                                  │
├─────────────────────────────────────────────────────────────────┤
│  FLASH_KV_HASH_SIZE = 1024 (必须是2的幂)                        │
├─────────────────────────────────────────────────────────────────┤
│  Slot[0]   →  {key_len, key, flash_offset}                     │
│  Slot[1]   →  {key_len, key, flash_offset}                     │
│  Slot[2]   →  {key_len, key, flash_offset}                     │
│  ...                                                           │
│  Slot[N]   →  {key_len, key, flash_offset}                     │
├─────────────────────────────────────────────────────────────────┤
│  哈希算法: DJB2                                                  │
│  h = 5381                                                      │
│  for each byte c in key:                                        │
│      h = ((h << 5) + h) + c  // h*33 + c                      │
│  index = h & (SIZE - 1)  // 快速取模                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. 数据结构

### 5.1 核心类型定义

```c
/* flash_kv_types.h */

/* 错误码定义 */
typedef enum {
    KV_OK = 0,                 // 成功
    KV_ERR_INVALID_PARAM = -1,  // 参数错误
    KV_ERR_NO_SPACE = -2,      // 空间不足
    KV_ERR_NOT_FOUND = -3,     // 键不存在
    KV_ERR_CRC_FAIL = -4,      // CRC校验失败
    KV_ERR_FLASH_FAIL = -5,    // Flash操作失败
    KV_ERR_TRANSACTION = -6,   // 事务错误
    KV_ERR_NO_INIT = -7,       // 未初始化
    KV_ERR_GC_FAIL = -8,       // GC失败
    KV_ERR_HASH_FULL = -10     // 哈希表满
} kv_err_t;

/* Flash操作接口 (用户实现) */
typedef struct {
    int (*init)(void);                                    // 初始化
    int (*read)(uint32_t addr, uint8_t *buf, uint32_t len); // 读取
    int (*write)(uint32_t addr, const uint8_t *buf, uint32_t len); // 写入
    int (*erase)(uint32_t addr, uint32_t len);           // 擦除
} flash_kv_ops_t;

/* KV记录 */
typedef struct {
    uint8_t key[FLASH_KV_KEY_SIZE];     // 32字节Key
    uint8_t value[FLASH_KV_VALUE_SIZE]; // 64字节Value
    uint8_t key_len;                     // 实际Key长度
    uint8_t value_len;                   // 实际Value长度
    uint8_t flags;                       // bit0:有效, bit1:删除
    uint8_t reserved[1];
    uint16_t crc16;                      // CRC-16校验
} __attribute__((packed)) kv_record_t;

/* KV句柄 */
typedef struct kv_handle {
    uint8_t instance_id;                  // 实例ID
    uint32_t active_region;              // 当前活跃区域 (0=A, 1=B)
    uint32_t version;                     // 版本号
    uint32_t record_count;               // 记录数
    kv_tx_state_persist_t tx_state;      // 事务状态
    uint32_t region_addr[2];             // A/B区域地址
    uint32_t region_size;                 // 区域大小
    uint32_t block_size;                 // Flash块大小
    const flash_kv_ops_t *ops;           // Flash操作接口
} kv_handle_t;

/* 哈希表槽 */
typedef struct {
    uint8_t key_len;                      // Key长度
    uint8_t key[FLASH_KV_KEY_SIZE];      // Key数据
    uint32_t flash_offset;                // Flash偏移
} kv_hash_slot_t;

/* 哈希表 */
typedef struct {
    kv_hash_slot_t slots[FLASH_KV_HASH_SIZE]; // 槽数组
    uint16_t count;                            // 已用槽数
} kv_hash_table_t;
```

---

## 6. 接口说明

### 6.1 初始化接口

```c
/**
 * @brief 注册Flash适配器
 * @param ops Flash操作接口
 * @return 0成功, 负值失败
 *
 * 使用示例:
 *   flash_kv_adapter_register(&stm32_flash_ops);
 */
int flash_kv_adapter_register(const flash_kv_ops_t *ops);

/**
 * @brief 初始化KV存储
 * @param instance_id 实例ID (0或1)
 * @param config 配置参数
 * @return 0成功, 负值失败
 *
 * 使用示例:
 *   kv_instance_config_t config = {
 *       .start_addr = 0x0800F000,
 *       .total_size = 32 * 1024,
 *       .block_size = 2048,
 *   };
 *   flash_kv_init(0, &config);
 */
int flash_kv_init(uint8_t instance_id, const kv_instance_config_t *config);
```

### 6.2 基本操作接口

```c
/**
 * @brief 设置键值对
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 0成功, 负值失败
 *
 * 注意:
 *   - 如果key已存在, 则更新value
 *   - 如果空间不足, 自动触发GC
 *   - GC后仍空间不足, 返回KV_ERR_NO_SPACE
 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len);

/**
 * @brief 获取键值对
 * @param key 键
 * @param key_len 键长度
 * @param value 值缓冲区
 * @param value_len [in]缓冲区大小, [out]实际值长度
 * @return 0成功, 负值失败
 *
 * 使用示例:
 *   uint8_t value[64];
 *   uint8_t len = sizeof(value);
 *   if (flash_kv_get("name", 4, value, &len) == 0) {
 *       printf("value=%s\n", value);
 *   }
 */
int flash_kv_get(const uint8_t *key, uint8_t key_len,
                 uint8_t *value, uint8_t *value_len);

/**
 * @brief 删除键值对
 * @param key 键
 * @param key_len 键长度
 * @return 0成功, 负值失败
 *
 * 注意: 实际是标记删除, 空间由GC回收
 */
int flash_kv_del(const uint8_t *key, uint8_t key_len);

/**
 * @brief 检查键是否存在
 * @param key 键
 * @param key_len 键长度
 * @return true存在, false不存在
 */
bool flash_kv_exists(const uint8_t *key, uint8_t key_len);
```

### 6.3 事务接口

```c
/**
 * @brief 开始事务
 * @return 0成功, 负值失败
 *
 * 事务说明:
 *   - 事务期间的所有操作在commit前不持久化
 *   - 可以嵌套 (仅顶层事务有效)
 *   - 断电自动回滚
 */
int flash_kv_tx_begin(void);

/**
 * @brief 提交事务
 * @return 0成功, 负值失败
 *
 * 注意: 仅当前活跃区域数据提交
 */
int flash_kv_tx_commit(void);

/**
 * @brief 回滚事务
 * @return 0成功, 负值失败
 */
int flash_kv_tx_rollback(void);
```

### 6.4 GC与维护接口

```c
/**
 * @brief 触发垃圾回收
 * @return 0成功, 负值失败
 *
 * GC过程:
 *   1. 切换到备用区域
 *   2. 复制所有有效记录到新区域
 *   3. 重建哈希表
 *   4. 更新区域头
 */
int flash_kv_gc(void);

/**
 * @brief 获取空闲空间百分比
 * @return 0-100 空闲百分比
 */
uint8_t flash_kv_free_percent(void);

/**
 * @brief 遍历所有键值对
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 0成功, 负值失败
 *
 * 回调函数格式:
 *   int callback(const uint8_t *key, uint8_t key_len,
 *                const uint8_t *value, uint8_t value_len,
 *                void *user_data);
 *   返回非0终止遍历
 */
int flash_kv_foreach(kv_foreach_cb callback, void *user_data);

/**
 * @brief 清空所有数据
 * @return 0成功, 负值失败
 */
int flash_kv_clear(void);

/**
 * @brief 获取记录总数
 * @return 记录数
 */
uint32_t flash_kv_count(void);

/**
 * @brief 获取存储状态
 * @param total [out]总空间
 * @param used  [out]已用空间
 * @return 0成功
 */
int flash_kv_status(uint32_t *total, uint32_t *used);
```

---

## 7. 核心流程

### 7.1 初始化流程

```
┌─────────────────────────────────────────────────────────────────┐
│                      flash_kv_init 流程                         │
└─────────────────────────────────────────────────────────────────┘

    ┌──────────────────┐
    │   开始初始化      │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │ 读取Region A头   │──── 失败 ────┐
    └────────┬─────────┘              │
             │                        │
             ▼                        │
    ┌──────────────────┐              │
    │ 读取Region B头   │              │
    └────────┬─────────┘              │
             │                        │
      ┌──────┴──────┐                 │
      │             │                 │
      ▼             ▼                 │
 ┌────────┐    ┌────────┐             │
 │ A有效  │    │ B有效  │             │
 └────┬───┘    └────┬───┘             │
      │             │                 │
      ▼             ▼                 │
 ┌────────────────────────┐           │
 │   选择版本号大的区域    │           │
 │   作为活跃区域          │           │
 └────────┬───────────────┘           │
          │                           │
          ▼                           │
 ┌──────────────────┐                 │
 │  重建内存哈希表   │                 │
 └────────┬─────────┘                 │
          │                           │
          ▼                           │
 ┌──────────────────┐                 │
 │  初始化完成       │◄───────────────┘
 └──────────────────┘
```

### 7.2 Set操作流程

```
┌─────────────────────────────────────────────────────────────────┐
│                      flash_kv_set 流程                          │
└─────────────────────────────────────────────────────────────────┘

    ┌──────────────────┐
    │   flash_kv_set   │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │  检查参数有效性   │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │ 哈希表查找key    │── 存在 ──► ┌──────────────────┐
    └────────┬─────────┘           │  标记旧记录删除   │
             │                     └────────┬─────────┘
             │不存在                           │
             ▼                                ▼
    ┌──────────────────┐            ┌──────────────────┐
    │ 检查空间是否足够  │── 不够 ──► │  触发GC          │
    └────────┬─────────┘           └────────┬─────────┘
             │足够                            │
             ▼                                │
    ┌──────────────────┐                     │
    │  写入新记录       │                     │
    │  (计算CRC16)     │                     │
    └────────┬─────────┘                     │
             │                                │
             ▼                                │
    ┌──────────────────┐                     │
    │  更新哈希表       │                     │
    └────────┬─────────┘                     │
             │                                │
             ▼                                │
    ┌──────────────────┐                     │
    │ 检查空闲空间<阈值 │── 是 ──► 触发GC    │
    └──────────────────┘                     │
             │                                │
             ▼                                │
    ┌──────────────────┐                     │
    │   返回结果       │◄────────────────────┘
    └──────────────────┘
```

### 7.3 GC流程

```
┌─────────────────────────────────────────────────────────────────┐
│                      flash_kv_gc 流程                           │
└─────────────────────────────────────────────────────────────────┘

    ┌──────────────────┐
    │   开始GC         │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │ 切换到备用区域    │
    │ (A→B 或 B→A)     │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │  擦除新区域       │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │ 扫描原区域有效记录│
    └────────┬─────────┘
             │
      ┌──────┴──────┐
      │             │
      ▼             ▼
  ┌───────┐    ┌───────┐
  │还有记录│    │无记录 │
  └──┬────┘    └──┬────┘
      │           │
      ▼           ▼
┌───────────┐  ┌───────────┐
│ 复制到新  │  │ 写入新头  │
│ 区域      │  │ (count=0) │
└─────┬─────┘  └─────┬─────┘
      │              │
      └──────┬───────┘
             │
             ▼
    ┌──────────────────┐
    │ 重建内存哈希表    │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │ 更新区域头        │
    │ (新版本号)       │
    └────────┬─────────┘
             │
             ▼
    ┌──────────────────┐
    │   GC完成          │
    └──────────────────┘
```

### 7.4 事务流程

```
┌─────────────────────────────────────────────────────────────────┐
│                    事务操作流程                                  │
└─────────────────────────────────────────────────────────────────┘

    ┌──────────────────────────────────────────────────────────┐
    │                    正常提交流程                            │
    ├──────────────────────────────────────────────────────────┤
    │                                                          │
    │  begin() ──► set(key1) ──► set(key2) ──► commit()      │
    │     │              │              │              │        │
    │     ▼              ▼              ▼              ▼        │
    │  TX_STATE_    TX_STATE_     TX_STATE_     TX_STATE_    │
    │  PREPARED     PREPARED      PREPARED      COMMITTED     │
    │                                                          │
    └──────────────────────────────────────────────────────────┘

    ┌──────────────────────────────────────────────────────────┐
    │                    回滚/断电恢复                          │
    ├──────────────────────────────────────────────────────────┤
    │                                                          │
    │  begin() ──► set(key1) ──► set(key2) ──► rollback()    │
    │     │              │              │              │        │
    │     ▼              ▼              ▼              ▼        │
    │  TX_STATE_    TX_STATE_     TX_STATE_     TX_STATE_      │
    │  PREPARED     PREPARED      PREPARED      IDLE           │
    │                                                          │
    │  (初始化时检测PREPARED状态, 自动回滚)                     │
    │                                                          │
    └──────────────────────────────────────────────────────────┘
```

---

## 8. 移植指南

### 8.1 快速移植步骤

```
┌─────────────────────────────────────────────────────────────────┐
│                      移植步骤概览                                │
├─────────────────────────────────────────────────────────────────┤
│  1. 复制源文件到项目                                            │
│     cp flash_kv/inc/*.h your_project/inc/                     │
│     cp flash_kv/src/*.c your_project/src/                    │
│                                                                 │
│  2. 配置参数 (flash_kv_config.h)                               │
│     - FLASH_KV_BLOCK_SIZE    : Flash页/扇区大小                 │
│     - FLASH_KV_START_ADDR   : KV存储起始地址                   │
│     - FLASH_KV_TOTAL_SIZE   : KV存储总大小                     │
│                                                                 │
│  3. 实现Flash驱动                                               │
│     实现 flash_kv_ops_t 接口 (init/read/write/erase)         │
│                                                                 │
│  4. 初始化并使用                                                │
│     flash_kv_adapter_register(&your_flash_ops);               │
│     flash_kv_init(0, &config);                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 8.2 Flash驱动实现模板

```c
/**
 * @file your_flash.c
 * @brief 你的平台Flash驱动实现
 */

#include "flash_kv_types.h"

/* 1. 实现Flash操作接口 */
static int your_flash_init(void)
{
    /* 初始化Flash硬件/驱动 */
    return 0;
}

static int your_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    /* 读取Flash数据 */
    /* 注意: 地址是相对地址, 从KV存储区起始计算 */
    return 0;
}

static int your_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    /* 写入Flash数据 */
    /* 注意: 写入前需要确保对应区域已擦除 */
    /* 返回0成功, 负值失败 */
    return 0;
}

static int your_flash_erase(uint32_t addr, uint32_t len)
{
    /* 擦除Flash区域 */
    /* addr: 相对地址, len: 擦除长度 */
    /* 必须按块(页/扇区)擦除 */
    return 0;
}

/* 2. 注册到Flash KV */
const flash_kv_ops_t your_flash_ops = {
    .init   = your_flash_init,
    .read   = your_flash_read,
    .write  = your_flash_write,
    .erase  = your_flash_erase,
};
```

### 8.3 STM32适配示例

```c
/**
 * @brief STM32 Flash写入 (HAL库)
 */
static int stm32_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    HAL_StatusTypeDef status;
    uint32_t flash_addr = FLASH_BASE_ADDR + addr;

    HAL_FLASH_Unlock();

    /* 按半字(16bit)写入 */
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t halfword = buf[i] | (buf[i+1] << 8);
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                   flash_addr + i, halfword);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

/**
 * @brief STM32 Flash擦除
 */
static int stm32_flash_erase(uint32_t addr, uint32_t len)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error;
    uint32_t flash_addr = FLASH_BASE_ADDR + addr;

    HAL_FLASH_Unlock();

    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = flash_addr;
    erase_init.NbPages = (len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;

    HAL_FLASH_Erase(&erase_init, &page_error);
    HAL_FLASH_Lock();

    return 0;
}
```

### 8.4 链接脚本配置

确保在链接脚本中为KV存储预留空间:

```ld
/* STM32链接脚本示例 */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 192K
    KV_STORAGE : ORIGIN = 0x0800F000, LENGTH = 32K  /* 预留32KB */
}

SECTIONS
{
    /* KV存储区域 */
    .kv_storage :
    {
        . = ALIGN(4);
        *(.kv_storage)
        . = ALIGN(4);
    } >KV_STORAGE
}
```

### 8.5 平台配置对照表

| 平台 | BLOCK_SIZE | 写入要求 | 备注 |
|------|------------|----------|------|
| STM32F1 | 2KB | 半字对齐 | 按页擦除 |
| STM32F4 | 16KB/扇区 | 半字对齐 | 按扇区擦除 |
| STM32L4 | 4KB/页 | 双字对齐 | 按页擦除 |
| ESP32 | 4KB/扇区 | 按字节 | OTA需单独处理 |
| nRF52 | 4KB/页 | 按字节 | SoftDevice占用高地址 |

---

## 9. 调试与测试

### 9.1 常见错误码

```c
/**
 * 错误码速查
 * ================================
 * KV_OK              = 0   成功
 * KV_ERR_INVALID_PARAM = -1  参数错误 (key/value为空或超长)
 * KV_ERR_NO_SPACE    = -2   空间不足 (GC后仍不足)
 * KV_ERR_NOT_FOUND   = -3   键不存在 (get/del时key不存在)
 * KV_ERR_CRC_FAIL    = -4   CRC校验失败 (数据损坏)
 * KV_ERR_FLASH_FAIL  = -5   Flash操作失败
 * KV_ERR_TRANSACTION = -6   事务错误 (未begin就commit)
 * KV_ERR_NO_INIT     = -7   未初始化
 * KV_ERR_GC_FAIL     = -8   GC失败
 * KV_ERR_HASH_FULL   = -10  哈希表满 (超过512条)
 */
```

### 9.2 调试技巧

```c
/**
 * 调试方法1: 启用调试日志
 * 在 flash_kv_config.h 中添加:
 */
#define FLASH_KV_DEBUG 1

#if FLASH_KV_DEBUG
#define KV_DEBUG(fmt, ...)  printf("[KV] " fmt "\n", ##__VA_ARGS__)
#else
#define KV_DEBUG(fmt, ...)
#endif

/**
 * 调试方法2: 查看存储状态
 */
void dump_kv_status(void)
{
    uint32_t total, used;
    flash_kv_status(&total, &used);
    printf("Total: %u bytes, Used: %u bytes\n", total, used);
    printf("Free: %u%%\n", flash_kv_free_percent());
    printf("Records: %u\n", flash_kv_count());
}

/**
 * 调试方法3: 遍历打印所有键值对
 */
void dump_all_kvs(void)
{
    printf("\n=== All KV Pairs ===\n");
    flash_kv_foreach(NULL, NULL);  // 先统计

    int callback(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len,
                 void *user_data)
    {
        printf("%.*s = %.*s\n", key_len, key, value_len, value);
        return 0;
    }
    flash_kv_foreach(callback, NULL);
}

/**
 * 调试方法4: 直接读取Flash
 */
void dump_flash_hex(uint32_t addr, uint32_t len)
{
    uint8_t buf[256];
    your_flash_read(addr, buf, len);

    for (uint32_t i = 0; i < len; i += 16) {
        printf("%04X: ", addr + i);
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            printf("%02X ", buf[i + j]);
        }
        printf("\n");
    }
}
```

### 9.3 测试用例说明

```c
/**
 * 运行测试: ./flash_kv_test
 *
 * 测试用例列表:
 * ================================
 * [1] test_kv_init         - 初始化测试
 * [2] test_kv_set_get      - 基本读写测试
 * [3] test_kv_update       - 更新测试
 * [4] test_kv_delete      - 删除测试
 * [5] test_kv_exists       - 存在性检查
 * [6] test_kv_clear       - 清空测试
 * [7] test_kv_count        - 计数测试
 * [8] test_kv_foreach      - 遍历测试
 * [9] test_kv_reinit      - 重新初始化测试
 * [10] test_kv_gc         - 垃圾回收测试
 * [11] test_kv_transaction - 事务测试
 * [12] test_kv_dual_region - 双区域备份测试
 */

/**
 * 手动运行特定测试:
 */
int main(void)
{
    mock_flash_init();
    flash_kv_adapter_register(&mock_flash_ops);

    // 只运行GC测试
    test_kv_gc();

    return 0;
}
```

---

## 10. 性能优化

### 10.1 性能参数

| 操作 | 时间复杂度 | 典型耗时 |
|------|-----------|----------|
| get | O(1) | <1ms |
| set | O(1) | 5-10ms |
| del | O(1) | 5-10ms |
| gc | O(n) | 50-200ms |
| foreach | O(n) | 10-50ms |

### 10.2 优化建议

1. **批量操作使用事务**
   ```c
   flash_kv_tx_begin();
   for (int i = 0; i < 10; i++) {
       flash_kv_set(keys[i], ..., values[i], ...);
   }
   flash_kv_tx_commit();  // 一次性写入，减少擦除
   ```

2. **合理设置GC阈值**
   ```c
   // flash_kv_config.h
   #define FLASH_KV_GC_THRESHOLD  20  // 空闲<20%时自动GC
   ```

3. **避免频繁小写入**
   - 合并小数据为结构体
   - 使用定长数据减少碎片

4. **RAM优化**
   - 哈希表默认全加载到RAM
   - 可修改为按需加载(需自行实现)

---

## 11. 常见问题

### Q1: 写入返回-5 (KV_ERR_FLASH_FAIL)

**原因**: Flash写入失败
**排查**:
1. 检查地址是否超出范围
2. 检查对应区域是否已擦除
3. 检查Flash驱动返回值

### Q2: 读取返回-4 (KV_ERR_CRC_FAIL)

**原因**: 数据校验失败
**排查**:
1. Flash数据损坏 (可能是写入过程中断电)
2. 重新初始化或清除数据

### Q3: 返回-2 (KV_ERR_NO_SPACE)

**原因**: 空间不足
**排查**:
1. 调用 `flash_kv_gc()` 回收空间
2. 检查 `flash_kv_free_percent()` 空闲率
3. 删除不需要的键值对

### Q4: 初始化失败

**原因**: Flash区域无效
**排查**:
1. 首次使用先调用 `flash_kv_clear()`
2. 检查Flash起始地址和大小配置
3. 确认链接脚本预留了足够空间

### Q5: 断电后数据丢失

**原因**: 写入未完成
**排查**:
1. 确保写入后有足够等待时间
2. 检查事务是否正确提交
3. 验证备用区域是否正常

---

## 附录

### A. 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-02-25 | 初始版本 |

### B. 参考资料

- STM32F4 Reference Manual
- CRC-16-CCITT specification
- DJB2 hash algorithm

### C. 许可协议

本项目使用 MIT 许可证，详见 LICENSE 文件。

---

*文档最后更新: 2026-02-25*
