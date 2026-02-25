# Flash KV 模块编码规范

## 1. 概述

本文档定义Flash KV参数存储模块的编码规范，适用于C语言嵌入式开发。

## 2. 代码质量原则

### 2.1 核心原则

| 原则 | 说明 |
|------|------|
| **可读性优先** | 代码阅读次数远超编写次数，优先保证清晰易懂 |
| **KISS** | 保持简单，避免过度设计 |
| **DRY** | 避免重复，提取公共逻辑 |
| **YAGNI** | 不要预先构建功能，只实现当前需要的 |

### 2.2 嵌入式特定原则

| 原则 | 说明 |
|------|------|
| **资源意识** | 时刻考虑RAM/ROM限制，避免不必要的内存分配 |
| **确定性** | 优先使用静态内存分配，运行时避免动态分配 |
| **中断安全** | 关键代码考虑中断上下文安全性 |
| **可移植性** | 平台相关代码隔离到适配层 |

---

## 3. 命名规范

### 3.1 总体规则

- 使用**英文**命名，避免拼音
- 变量名/函数名使用`snake_case`
- 宏定义/枚举常量使用`UPPER_SNAKE_CASE`
- 类型定义使用`snake_case_t`后缀

### 3.2 具体规范

| 类型 | 规则 | 示例 |
|------|------|------|
| 文件名 | 全小写，下划线分隔 | `flash_kv_core.c` |
| 函数名 | 动词前缀 + 模块名 | `flash_kv_set()`, `kv_hash_get()` |
| 变量名 | 具体用途命名 | `active_region`, `record_count` |
| 宏定义 | 全大写，模块前缀 | `FLASH_KV_BLOCK_SIZE` |
| 类型定义 | `snake_case_t` | `kv_err_t`, `kv_handle_t` |
| 枚举值 | 前缀+大写 | `KV_OK`, `KV_ERR_NOT_FOUND` |
| 结构体成员 | 全小写 | `key_len`, `flash_offset` |

### 3.3 命名禁忌

```c
// ❌ 避免：缩写过度
int n;           // 不知道是什么
int tmp;         // 临时变量可接受，但尽量用有意义的名字

// ❌ 避免：拼音
int shezhi;      // 设置

// ❌ 避免：单个字母（循环变量除外）
int x, y, z;

// ✅ 推荐：清晰的命名
int error_code;
uint32_t record_count;
bool is_initialized;
```

---

## 4. 代码格式

### 4.1 缩进与空格

- 使用**4空格**缩进（不是Tab）
- 行长度限制：**100字符**以内
- 函数之间空**2行**
- 代码块之间空**1行**

```c
/* ✅ 正确示例 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len)
{
    if (key == NULL || value == NULL) {
        return KV_ERR_INVALID_PARAM;
    }

    /* 业务逻辑 */
    return KV_OK;
}

/* ❌ 错误示例 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
const uint8_t *value, uint8_t value_len){
if(key==NULL||value==NULL){
return KV_ERR_INVALID_PARAM;
}
return KV_OK;
}
```

### 4.2 大括号

- 函数用**开括号另起一行**
- 控制语句（if/for/while）**同一行**开括号

```c
/* ✅ 函数：大括号另起一行 */
int func(void)
{
    // ...
}

/* ✅ 控制语句：同一行 */
if (condition) {
    // ...
} else {
    // ...
}

/* ❌ 错误 */
if (condition)
{
    // ...
}
```

### 4.3 空格规则

```c
/* ✅ 运算符前后有空格 */
a = b + c;
if (a == b) {
    for (int i = 0; i < 10; i++) {
        func(a, b);
    }
}

/* ❌ 错误：缺少空格 */
a=b+c;
if(a==b){
    for(int i=0;i<10;i++){
        func(a,b);
    }
}

/* ✅ 指针类型 * 靠近变量名 */
const uint8_t *key;
int *data;

/* ❌ 错误：* 靠近类型 */
const uint8_t* key;
int* data;
```

---

## 5. 函数设计

### 5.1 函数长度

- **单个函数不超过100行**
- 超过需拆分

### 5.2 函数参数

- 参数不超过**5个**
- 超过5个使用结构体封装
- 输入参数用`const`

```c
/* ✅ 参数合理 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len);

/* ✅ 参数过多时用结构体 */
typedef struct {
    const uint8_t *key;
    uint8_t key_len;
    const uint8_t *value;
    uint8_t value_len;
    uint32_t timeout_ms;
    bool atomic;
} kv_write_params_t;

int flash_kv_write_ex(const kv_write_params_t *params);
```

### 5.3 返回值

- 使用错误码返回（不要用全局变量）
- 成功返回0或正数，失败返回负错误码
- 布尔类型用`bool`（需要`<stdbool.h>`）

```c
/* ✅ 错误码定义 */
typedef enum {
    KV_OK = 0,
    KV_ERR_INVALID_PARAM = -1,
    KV_ERR_NO_SPACE = -2,
    KV_ERR_NOT_FOUND = -3,
} kv_err_t;

/* ✅ 函数返回错误码 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len)
{
    if (key == NULL) {
        return KV_ERR_INVALID_PARAM;
    }
    return KV_OK;
}

/* ✅ 布尔类型 */
bool flash_kv_exists(const uint8_t *key, uint8_t key_len);
```

---

## 6. 注释规范

### 6.1 注释原则

- **解释WHY，不解释WHAT**
- 代码本身能表达的不需要注释
- 复杂逻辑、业务背景需要注释

```c
/* ✅ 正确：解释原因 */
// Use exponential backoff to avoid overwhelming Flash during writes
for (int retry = 0; retry < 3; retry++) {
    // ...
}

/* ✅ 正确：解释复杂算法 */
// Hash table uses linear probing for collision resolution.
// When a slot is occupied, we check the next slot until
// an empty slot is found or the table is full.

/* ❌ 错误：解释显而易见的事 */
// Increment counter
count++;

/* ❌ 错误：代码本身已经说明 */
// Set error code to -1
err = -1;
```

### 6.2 文档注释

公共API使用Doxygen风格注释：

```c
/**
 * @brief 设置键值对
 *
 * @param key 键（不可为空）
 * @param key_len 键长度（不超过FLASH_KV_KEY_SIZE）
 * @param value 值（不可为空）
 * @param value_len 值长度（不超过FLASH_KV_VALUE_SIZE）
 *
 * @return KV_OK 成功
 * @return KV_ERR_INVALID_PARAM 参数无效
 * @return KV_ERR_NO_SPACE 空间不足
 *
 * @note 如果Key已存在，则更新Value
 */
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len);
```

---

## 7. 错误处理

### 7.1 参数校验

- 所有公共函数必须校验参数
- 使用`assert()`做开发期检查，用实际错误码做运行期检查

```c
int flash_kv_set(const uint8_t *key, uint8_t key_len,
                 const uint8_t *value, uint8_t value_len)
{
    /* 开发期检查：捕获编程错误 */
    assert(key != NULL);
    assert(value != NULL);

    /* 运行期检查：处理运行时错误 */
    if (key_len == 0 || key_len > FLASH_KV_KEY_SIZE) {
        return KV_ERR_INVALID_PARAM;
    }

    if (value_len == 0 || value_len > FLASH_KV_VALUE_SIZE) {
        return KV_ERR_INVALID_PARAM;
    }

    return KV_OK;
}
```

### 7.2 资源管理

- 谁分配谁释放
- 函数出口统一处理资源释放
- 使用`goto`处理多出口是允许的

```c
int flash_kv_operation(void)
{
    uint8_t *buffer = NULL;
    int ret = KV_OK;

    buffer = malloc(256);
    if (buffer == NULL) {
        return KV_ERR_NO_MEMORY;
    }

    ret = do_operation(buffer);
    if (ret != KV_OK) {
        goto cleanup;
    }

    ret = do_other_operation();
    if (ret != KV_OK) {
        goto cleanup;
    }

cleanup:
    free(buffer);
    return ret;
}
```

---

## 8. 头文件规范

### 8.1 头文件结构

```c
#ifndef FLASH_KV_H
#define FLASH_KV_H

/*============================================================================
 * Includes
 *============================================================================*/
#include "flash_kv_config.h"
#include "flash_kv_types.h"
#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Macros
 *============================================================================*/

/*============================================================================
 * Type Definitions
 *============================================================================*/

/*============================================================================
 * Function Declarations
 *============================================================================*/

/**
 * @brief 初始化
 */
int flash_kv_init(void);

/**
 * @brief 去初始化
 */
int flash_kv_deinit(void);

#endif /* FLASH_KV_H */
```

### 8.2 头文件保护

- 必须有`#ifndef`/`#define`/`#endif`保护
- 命名格式：`文件名_H`

---

## 9. 常量与宏

### 9.1 魔法数字

- 禁止使用魔法数字
- 使用有意义的宏或枚举

```c
/* ❌ 魔法数字 */
if (count > 10) {
    // ...
}

/* ✅ 有意义常量 */
#define MAX_RETRY_COUNT   10
if (count > MAX_RETRY_COUNT) {
    // ...
}
```

### 9.2 数值类型

- 使用固定宽度整数类型（`<stdint.h>`）
- 区分有符号/无符号

| 类型 | 用途 |
|------|------|
| `int8_t` / `uint8_t` | 8位有符号/无符号 |
| `int16_t` / `uint16_t` | 16位有符号/无符号 |
| `int32_t` / `uint32_t` | 32位有符号/无符号 |
| `intptr_t` | 指针（足够容纳指针） |
| `size_t` | 对象大小、数组索引 |

---

## 10. 测试规范

### 10.1 测试文件命名

```
test/
├── flash_kv_test.c       # 单元测试
└── mock_flash.c          # Mock驱动
```

### 10.2 测试函数命名

```c
/* ✅ 清晰的测试名称 */
void test_kv_set_basic(void);
void test_kv_get_not_found(void);
void test_kv_update_existing_key(void);
void test_kv_gc_triggered(void);

/* ❌ 模糊的测试名称 */
void test1(void);
void test_set(void);
```

### 10.3 测试结构（AAA模式）

```c
void test_kv_set_basic(void)
{
    /* Arrange - 准备测试数据 */
    const uint8_t key[] = "test_key";
    const uint8_t value[] = "test_value";

    /* Act - 执行被测操作 */
    int ret = flash_kv_set(key, sizeof(key) - 1,
                           value, sizeof(value) - 1);

    /* Assert - 验证结果 */
    TEST_ASSERT_EQUAL(KV_OK, ret);
    TEST_ASSERT_TRUE(flash_kv_exists(key, sizeof(key) - 1));
}
```

---

## 11. 版本管理

### 11.1 提交信息格式

```
<type>: <简短描述>

<详细说明（可选）>

Co-Authored-By: Claude <noreply@anthropic.com>
```

### 11.2 Type类型

| Type | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug修复 |
| `docs` | 文档更新 |
| `style` | 代码格式（不影响功能） |
| `refactor` | 重构（既不是新功能也不是修复） |
| `test` | 测试相关 |
| `chore` | 构建/工具相关 |

### 11.3 示例

```
feat: 实现哈希表索引机制

- 采用开放地址法哈希表
- 实现DJB2哈希函数
- 支持O(1)查找复杂度

fix: 修复GC后索引未重建问题

Co-Authored-By: Claude <noreply@anthropic.com>
```

---

## 12. 检查清单

新代码提交前检查：

- [ ] 命名清晰，符合规范
- [ ] 代码格式正确（缩进、空格、大括号）
- [ ] 关键函数有文档注释
- [ ] 参数有校验
- [ ] 错误码返回正确
- [ ] 无魔法数字
- [ ] 无内存泄漏风险
- [ ] 已通过单元测试
