cmake_minimum_required(VERSION 3.15)

project(stm32_flash_kv_demo C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# STM32工具链
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

# 交叉编译器
set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}as)
set(CMAKE_LINKER ${TOOLCHAIN_PREFIX}ld)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

# STM32 HAL库路径 (需要根据实际安装路径修改)
set(HAL_PATH $ENV{STM32CUBEF4_PATH}/Drivers)

# 项目源文件
set(SOURCES
    main.c
    stm32_flash.c
)

# 头文件路径
set(INCLUDES
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/src
    ${HAL_PATH}/STM32F4xx_HAL_Driver/Inc
    ${HAL_PATH}/CMSIS/Device/ST/STM32F4xx/Include
    ${HAL_PATH}/CMSIS/Include
)

# 链接Flash KV库
set(FLASH_KV_LIB ${CMAKE_SOURCE_DIR}/../../build/libflash_kv.a)

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-m4 -mthumb -mfloat-abi=soft")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_HAL_DRIVER -DSTM32F407xx")

# 添加可执行文件
add_executable(${PROJECT_NAME}.elf ${SOURCES})

# 设置头文件
target_include_directories(${PROJECT_NAME}.elf PRIVATE ${INCLUDES})

# 链接库
target_link_libraries(${PROJECT_NAME}.elf
    ${FLASH_KV_LIB}
    -lc
    -lm
    -lnosys
)

# 生成bin文件
add_custom_command(OUTPUT ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
    DEPENDS ${PROJECT_NAME}.elf
)

# 显示大小
add_custom_command(OUTPUT ${PROJECT_NAME}_size
    COMMAND ${CMAKE_SIZE} -A -d ${PROJECT_NAME}.elf
    DEPENDS ${PROJECT_NAME}.elf
)
