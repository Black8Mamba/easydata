# Flash KV STM32 Demo

基于Flash的Key-Value存储库STM32适配器示例

## 硬件要求

- STM32F4xx 系列MCU (推荐 STM32F407VG)
- 至少32KB Flash空间用于KV存储

## 目录结构

```
demo/stm32/
├── main.c              # 示例程序
├── stm32_flash.c       # STM32 Flash适配器实现
├── stm32_flash.h       # 适配器头文件
├── Makefile            # 构建脚本
├── CMakeLists.txt      # CMake配置
├── stm32f407vg.ld     # 链接脚本
└── README.md          # 本文件
```

## 快速开始

### 1. 准备STM32 HAL库

```bash
# 克隆STM32CubeF4到指定目录
git clone https://github.com/STMicroelectronics/STM32CubeF4.git /opt/STM32CubeF4
# 或设置环境变量指向你的HAL库路径
export STM32CUBEF4_PATH=/path/to/STM32CubeF4
```

### 2. 编译

```bash
cd demo/stm32
make
```

### 3. 烧录

```bash
# 使用ST-Link烧录
make flash

# 或使用J-Link
openocd -f interface/jlink.cfg -f target/stm32f4x.cfg -c "program flash_kv_demo.bin 0x08000000 verify reset exit"
```

### 4. 调试

```bash
# 打开串口查看输出
make monitor
# 或
minicom -D /dev/ttyUSB0 -b 115200
```

## 功能示例

Demo程序包含以下示例:

1. **基本操作** - set/get/del/exists
2. **配置存储** - 保存WiFi密码、系统参数等
3. **事务操作** - 批量操作的原子性
4. **遍历操作** - 遍历所有键值对
5. **垃圾回收** - 手动/自动GC

## API使用

### 初始化

```c
#include "flash_kv.h"
#include "stm32_flash.h"

// 1. 注册Flash适配器
flash_kv_adapter_register(&stm32_flash_ops);

// 2. 配置KV存储
kv_instance_config_t config = {
    .start_addr = 0x0800F000,  // KV存储起始地址
    .total_size = 32 * 1024,   // 32KB
    .block_size = 2048,        // Flash页大小
};
flash_kv_init(0, &config);
```

### 基本操作

```c
// 设置键值
flash_kv_set("key", 3, "value", 5);

// 读取键值
uint8_t value[64];
uint8_t len = sizeof(value);
flash_kv_get("key", 3, value, &len);

// 删除键值
flash_kv_del("key", 3);

// 检查存在
if (flash_kv_exists("key", 3)) {
    // key存在
}
```

### 事务

```c
flash_kv_tx_begin();
flash_kv_set("key1", 4, "val1", 4);
flash_kv_set("key2", 4, "val2", 4);
flash_kv_tx_commit();  // 或 flash_kv_tx_rollback()
```

### GC

```c
// 手动触发GC
flash_kv_gc();

// 查询空闲空间
uint8_t free_pct = flash_kv_free_percent();
```

## 配置说明

### 修改KV存储区域

在 `stm32_flash.c` 中修改:

```c
#define KV_FLASH_START_ADDR     0x0800F000  // 起始地址
#define KV_FLASH_SIZE           (32 * 1024) // 大小
```

或在 `stm32f407vg.ld` 链接脚本中修改 `.kv_storage` 区域。

### 适配其他STM32型号

修改 `stm32_flash.c` 中的宏:

```c
#if defined(STM32F1xx)
    // STM32F1配置
#elif defined(STM32F4xx)
    // STM32F4配置
#elif defined(STM32L4xx)
    // STM32L4配置
#endif
```

## 注意事项

1. **Flash写入限制**: STM32 Flash只能从1写成0，重复写入前必须先擦除
2. **对齐要求**: 写入必须按半字(16bit)对齐
3. **存储区域**: 确保KV存储区域不在应用程序代码区
4. **备份**: 首次使用建议调用 `flash_kv_clear()` 初始化

## 常见问题

**Q: 写入失败返回-1**
A: 检查是否正确解锁Flash，地址是否对齐

**Q: 数据读取错误**
A: 确认读取地址在有效范围内，检查Flash是否已擦除

**Q: GC后数据丢失**
A: GC过程确保不断电，可增加 `FLASH_KV_WRITE_RETRY_MAX` 重试次数
