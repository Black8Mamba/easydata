# EasyData

<p align="center">
  <img src="https://img.shields.io/badge/language-C-blue.svg" alt="Language">
  <img src="https://img.shields.io/badge/platform-Embeded%20%7C%20Linux-green.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-yellow.svg" alt="License">
</p>

为嵌入式系统设计的轻量级数据处理组件库。

## 组件列表

### Flash KV

专为微控制器设计的Flash Key-Value存储库。

详细文档：[flash_kv/README.md](./flash_kv/README.md)

#### 主要特性

- **平台无关** - 简洁的Flash抽象层，易于移植到任何MCU（STM32、ESP32、Nordic等）
- **日志式存储** - 追加写入模式，优秀的Flash磨损均衡
- **双区域备份(A/B)** - 掉电安全，支持自动区域切换
- **垃圾回收** - 内置GC，支持可配置阈值
- **哈希表索引** - O(1)查找性能
- **数据完整性** - 每条记录带有CRC-16校验
- **类型辅助函数** - 支持常见数据类型（u8、i8、u16、u32、float、double、bool）
- **Linux支持** - 内存模拟Flash，便于PC端单元测试

#### 快速开始

```bash
# 克隆仓库
git clone https://github.com/Black8Mamba/easydata.git
cd easydata/flash_kv

# 创建构建目录
mkdir build && cd build

# CMake配置
cmake ..

# 编译
make

# 运行测试
./flash_kv_test
```

#### 预期输出

```
=== Flash KV Unit Tests ===
Test: KV set/get... PASS
Test: KV update... PASS
Test: KV not found... PASS
Test: Type utils... PASS

=== All Tests PASSED ===
```

## 项目结构

```
easydata/
├── docs/                   # 文档
├── flash_kv/              # Flash KV存储组件
│   ├── inc/               # 头文件
│   ├── src/               # 源文件
│   ├── test/              # 测试文件
│   ├── README.md          # 组件文档
│   └── LICENSE            # MIT许可证
├── README.md              # 本文件
└── README_en.md           # English version
```

## 许可证

MIT License - 详见 [LICENSE](LICENSE) 文件

## 贡献

欢迎提交Pull Request！

---

<p align="center">为嵌入式系统而生 ❤️</p>
