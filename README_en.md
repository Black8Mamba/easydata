# EasyData

<p align="center">
  <img src="https://img.shields.io/badge/language-C-blue.svg" alt="Language">
  <img src="https://img.shields.io/badge/platform-Embeded%20%7C%20Linux-green.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-yellow.svg" alt="License">
</p>

A lightweight data processing library suite designed for embedded systems.

## Components

### Flash KV

A Flash-based Key-Value storage library designed for microcontrollers.

Documentation: [flash_kv/README.md](./flash_kv/README.md)

#### Features

- **Platform Independent** - Minimal Flash abstraction layer, easily adaptable to any MCU (STM32, ESP32, Nordic, etc.)
- **Log-Based Storage** - Append-only write mode, excellent for Flash wear leveling
- **Dual Region Backup (A/B)** - Power-fail safe with automatic region switching
- **Garbage Collection** - Built-in GC with configurable threshold
- **Hash Table Indexing** - O(1) lookup performance
- **Data Integrity** - CRC-16 checksum for each record
- **Type Helpers** - Utility functions for common data types (u8, i8, u16, u32, float, double, bool)
- **Linux Support** - Memory-mocked Flash for easy unit testing on PC

#### Quick Start

```bash
# Clone the repository
git clone https://github.com/Black8Mamba/easydata.git
cd easydata/flash_kv

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make

# Run tests
./flash_kv_test
```

#### Expected Output

```
========================================
     Flash KV Unit Tests
========================================

  [Test] KV Set/Get Basic Operations
  [-] SET: key="device_name", value="sensor_001"
  [+] SET OK
  [-] GET: key="device_name", value="sensor_001" (len=64)
  [+] GET OK, value match!
  ...

  [Test] KV Update (Same Key, Different Value)
  [-] SET: key="firmware_version", value="v1.0.0"
  [-] UPDATE: key="firmware_version", value="v1.0.1"
  ...

  [Test] Stress Test - Multiple KV Pairs
  [-] Progress: 10/50
  [-] Progress: 20/50
  ...
  [+] Wrote 50 keys
  [+] Verified 50 keys

========================================
     All Tests PASSED!
========================================
```

#### Test Cases

- **Basic Operations** - Set/Get/Exists
- **Update** - Same key with different value
- **Delete** - Delete key-value pairs
- **Not Found** - Get non-existent key
- **Type Utils** - u8/u16/u32/float/double/bool conversion
- **Boundary** - Empty key/value, oversized key/value
- **Clear** - Clear all data
- **Status** - Storage usage query
- **Stress Test** - Batch write and verify 50 key-value pairs

## Project Structure

```
easydata/
├── docs/                   # Documentation
├── flash_kv/              # Flash KV storage component
│   ├── inc/               # Header files
│   ├── src/               # Source files
│   ├── test/              # Test files
│   ├── README.md          # Component documentation
│   └── LICENSE            # MIT License
├── README.md              # This file (Chinese)
└── README_en.md           # English version
```

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

---

<p align="center">Made with ❤️ for embedded systems</p>
