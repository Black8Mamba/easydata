# Flash KV

基于Flash的嵌入式Key-Value存储库，专为MCU设计。

## 简介

Flash KV 是一个轻量级的持久化键值存储库，基于MCU内部Flash实现。相比传统EEPROM，具有容量大、速度快、无需额外硬件等优势。

## 特性

- **简单易用**: 类似NoSQL的API设计，set/get/del一步到位
- **掉电安全**: 双区域备份，事务支持，断电不丢失
- **垃圾回收**: 自动/手动GC，空间自动回收
- **数据完整**: CRC校验，确保数据可靠
- **体积小巧**: ~1500行C代码，RAM占用~2KB
- **易于移植**: 抽象Flash操作接口，适配任意MCU

## 快速开始

```c
#include "flash_kv.h"

// 1. 注册Flash驱动
flash_kv_adapter_register(&your_flash_ops);

// 2. 配置并初始化
kv_instance_config_t config = {
    .start_addr = 0x0800F000,
    .total_size = 32 * 1024,
    .block_size = 2048,
};
flash_kv_init(0, &config);

// 3. 使用KV存储
flash_kv_set("wifi_ssid", 9, "MyWiFi", 6);
flash_kv_set("baud_rate", 8, "115200", 6);

uint8_t value[64];
uint8_t len = sizeof(value);
flash_kv_get("wifi_ssid", 9, value, &len);  // value = "MyWiFi"
```

## 文档

详细文档请阅读: [Flash KV 设计与实现文档](docs/flash_kv_design.md)

包含内容:
- 功能特性介绍
- 系统架构设计
- 核心数据结构
- API接口说明
- 核心流程图
- 移植指南
- 调试方法
- 常见问题

## 项目结构

```
flash_kv/
├── inc/                    # 公开头文件
│   ├── flash_kv.h         # 主API
│   ├── flash_kv_types.h   # 类型定义
│   └── flash_kv_config.h  # 配置文件
├── src/                    # 核心实现
│   ├── flash_kv_core.c   # 核心逻辑
│   ├── flash_kv_hash.c   # 哈希表
│   ├── flash_kv_crc.c    # CRC校验
│   └── flash_kv_utils.c  # 工具函数
├── test/                   # 单元测试
├── demo/
│   └── stm32/            # STM32示例
└── docs/
    └── flash_kv_design.md # 详细设计文档
```

## 编译测试

```bash
mkdir build && cd build
cmake ..
make
./flash_kv_test
```

## STM32移植

参考 [STM32 Demo](demo/stm32/) 目录，包含完整示例代码。

## License

MIT License - see [LICENSE](../LICENSE)
