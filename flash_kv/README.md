# Flash KV

<p align="center">
  <img src="https://img.shields.io/badge/language-C-blue.svg" alt="Language">
  <img src="https://img.shields.io/badge/platform-Embeded%20%7C%20Linux-green.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-yellow.svg" alt="License">
  <img src="https://img.shields.io/badge/cmake-3.10+-orange.svg" alt="CMake">
</p>

A lightweight, platform-independent Key-Value storage library designed for embedded systems. Built on Flash memory with log-based storage, GC support, and dual-region backup for reliable data persistence.

## Features

- **Platform Independent** - Minimal Flash abstraction layer, easily adaptable to any MCU (STM32, ESP32, Nordic, etc.)
- **Log-Based Storage** - Append-only write mode, excellent for Flash wear leveling
- **Dual Region Backup (A/B)** - Power-fail safe with automatic region switching
- **Garbage Collection** - Built-in GC with configurable threshold
- **Hash Table Indexing** - O(1) lookup performance
- **Data Integrity** - CRC-16 checksum for each record
- **Type Helpers** - Utility functions for common data types (u8, i8, u16, u32, float, double, bool)
- **Linux Support** - Memory-mocked Flash for easy unit testing on PC

## Quick Start

### Build on Linux

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

### Expected Output

```
=== Flash KV Unit Tests ===
Test: KV set/get... PASS
Test: KV update... PASS
Test: KV not found... PASS
Test: Type utils... PASS

=== All Tests PASSED ===
```

## Project Structure

```
flash_kv/
├── inc/                    # Header files
│   ├── flash_kv_config.h   # Configuration macros
│   ├── flash_kv_types.h   # Type definitions
│   └── flash_kv.h         # Main API
├── src/                    # Source files
│   ├── flash_kv_core.c    # Core KV operations
│   ├── flash_kv_crc.c     # CRC16/CRC32 implementation
│   ├── flash_kv_hash.c    # Hash table
│   └── flash_kv_utils.c   # Type conversion utilities
├── test/                   # Test files
│   ├── flash_kv_test.c    # Unit tests
│   └── mock_flash.c       # Memory-mapped Flash simulation
└── CMakeLists.txt         # Build configuration
```

## Configuration

Modify `inc/flash_kv_config.h` to customize:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `FLASH_KV_BLOCK_SIZE` | 2048 | Flash erase block size (bytes) |
| `FLASH_KV_KEY_SIZE` | 32 | Key length (bytes) |
| `FLASH_KV_VALUE_SIZE` | 64 | Value length (bytes) |
| `FLASH_KV_MAX_RECORDS` | 512 | Maximum KV pairs |
| `FLASH_KV_HASH_SIZE` | 1024 | Hash table size (power of 2) |
| `FLASH_KV_GC_THRESHOLD` | 20 | GC trigger threshold (%) |

## API Usage

### Initialize

```c
#include "flash_kv.h"

// Register Flash operations (platform-specific)
flash_kv_adapter_register(&my_flash_ops);

// Configure and initialize
kv_instance_config_t config = {
    .start_addr = 0x0800F000,
    .total_size = 32 * 1024,
    .block_size = 2048,
};
flash_kv_init(0, &config);
```

### Basic Operations

```c
// Set a key-value pair
const uint8_t key[] = "device_name";
const uint8_t value[] = "sensor_01";
flash_kv_set(key, sizeof(key) - 1, value, sizeof(value) - 1);

// Get value
uint8_t read_val[64];
uint8_t len = sizeof(read_val);
flash_kv_get(key, sizeof(key) - 1, read_val, &len);

// Check existence
if (flash_kv_exists(key, sizeof(key) - 1)) {
    // Key exists
}

// Delete key
flash_kv_del(key, sizeof(key) - 1);
```

### Type Helpers

```c
#include "flash_kv_utils.h"

uint8_t buf[8];

// Store different types
kv_put_u32le(buf, 12345);
uint32_t val = kv_get_u32le(buf);

kv_put_float(buf, 3.14159f);
float f = kv_get_float(buf);

kv_put_bool(buf, true);
bool b = kv_get_bool(buf);
```

## Porting to Your MCU

Implement the `flash_kv_ops_t` interface in your platform code:

```c
const flash_kv_ops_t my_flash_ops = {
    .init   = my_flash_init,
    .read   = my_flash_read,
    .write  = my_flash_write,
    .erase  = my_flash_erase,
};
```

### Example: STM32 HAL

```c
static int stm32_flash_init(void) {
    HAL_FLASH_Unlock();
    return 0;
}

static int stm32_flash_read(uint32_t addr, uint8_t *buf, uint32_t len) {
    memcpy(buf, (void*)addr, len);
    return 0;
}

static int stm32_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i += 4) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, *(uint32_t*)(buf + i));
    }
    return 0;
}

static int stm32_flash_erase(uint32_t addr, uint32_t len) {
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = addr,
        .NbPages = len / FLASH_PAGE_SIZE
    };
    uint32_t error;
    HAL_FLASHEx_Erase(&erase, &error);
    return 0;
}
```

## Architecture

### Storage Format

```
+------------------+------------------+
|    Region A      |    Region B      |
| (active/backup) | (active/backup) |
+------------------+------------------+
| Header (magic,  | Header (magic,   |
|  version, etc.) |  version, etc.) |
+------------------+------------------+
| Record 1 (key,   | Record 1         |
|  value, crc)    |                  |
| Record 2        | Record 2         |
| ...             | ...              |
+------------------+------------------+
| Reserved Block  | Reserved Block   |
| (for atomic     | (for atomic      |
|  writes)        |  writes)         |
+------------------+------------------+
```

### Write Flow

1. Write new record to active region
2. Update hash table in memory
3. On region full, trigger GC or switch to backup region

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

---

<p align="center">Made with ❤️ for embedded systems</p>
