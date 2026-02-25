# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Flash KV is a lightweight, Flash-based key-value storage library for embedded MCUs. It provides persistent storage using internal MCU Flash with features like dual-region backup, garbage collection, and transaction support.

## Build Commands

```bash
# Build the library and tests
mkdir build && cd build
cmake ..
make

# Run all tests
./flash_kv_test
```

## Architecture

The project uses a layered architecture:

```
Application Layer (flash_kv.h API)
         │
         ▼
Core Layer (flash_kv_core.c - main logic, hash, GC, transactions)
         │
         ▼
Adapter Layer (flash_kv_ops_t interface - user implements)
         │
         ▼
Hardware Layer (STM32/ESP32/etc Flash)
```

### Key Components

- **flash_kv_core.c**: Core implementation including set/get/del, GC, transactions, dual-region backup
- **flash_kv_hash.c**: In-memory hash table for O(1) key lookups (DJB2 hash, open addressing)
- **flash_kv_crc.c**: CRC-16/CRC-32 checksum for data integrity
- **flash_kv_utils.c**: Type serialization utilities (u8/u16/u32/float/double/bool)

### Data Model

- **Dual-region (A/B)**: Two Flash regions alternate for crash safety
- **Record format**: key(32B) + value(64B) + metadata + CRC-16
- **Region header**: magic + version + record_count + active_offset + tx_state + CRC-32

## Key Files

| File | Purpose |
|------|---------|
| `inc/flash_kv.h` | Public API declarations |
| `inc/flash_kv_types.h` | Core type definitions |
| `inc/flash_kv_config.h` | Configuration (Flash params, KV sizes) |
| `src/flash_kv_core.c` | Main implementation (~800 lines) |
| `src/flash_kv_hash.c` | Hash table operations |
| `test/flash_kv_test.c` | 12 unit tests |
| `test/mock_flash.c` | Memory-mocked Flash for Linux testing |
| `demo/stm32/` | STM32 HAL adapter and example |

## Configuration

Key parameters in `flash_kv_config.h`:
- `FLASH_KV_BLOCK_SIZE`: Flash erase unit (default 2048)
- `FLASH_KV_KEY_SIZE`: Max key length (default 32)
- `FLASH_KV_VALUE_SIZE`: Max value length (default 64)
- `FLASH_KV_HASH_SIZE`: Hash table size (default 1024)
- `FLASH_KV_START_ADDR`: KV storage start address
- `FLASH_KV_TOTAL_SIZE`: Total KV storage size

## Testing

Tests are in `test/flash_kv_test.c`. All tests run together when executing `flash_kv_test`. Tests use a memory-mocked Flash driver (`mock_flash.c`) for cross-platform testing on Linux.

## Porting to New Platforms

Implement `flash_kv_ops_t` interface in `flash_kv_types.h`:
- `init()`: Initialize Flash hardware
- `read()`: Read from Flash
- `write()`: Write to Flash (must handle erase first)
- `erase()`: Erase Flash region

See `demo/stm32/stm32_flash.c` for STM32 HAL implementation reference.
