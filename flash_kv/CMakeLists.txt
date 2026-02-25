cmake_minimum_required(VERSION 3.10)
project(flash_kv C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/inc)

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
