# STM32 Flash KV Demo Makefile

# 工具链
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
SIZE = $(PREFIX)size

# 项目路径
PROJECT_ROOT = ../..
KV_LIB = $(PROJECT_ROOT)/build/libflash_kv.a

# STM32型号
DEVICE = STM32F407VG
CPU = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
FPU = -mfpu=fpv4-sp-d16

# HAL库路径 (根据实际安装修改)
STM32CUBE_PATH ?= /opt/STM32CubeF4
HAL_PATH = $(STM32CUBE_PATH)/Drivers
CMSIS_PATH = $(HAL_PATH)/CMSIS
HAL_INCLUDE = $(HAL_PATH)/STM32F4xx_HAL_Driver/Inc
DEVICE_INCLUDE = $(CMSIS_PATH)/Device/ST/STM32F4xx/Include
CMSIS_INCLUDE = $(CMSIS_PATH)/Include

# 编译选项
CFLAGS = $(CPU) $(FPU) -DUSE_HAL_DRIVER -DSTM32F407xx -D$(DEVICE)
CFLAGS += -Wall -Wextra -specs=nosys.specs -specs=nano.specs
CFLAGS += -I$(HAL_INCLUDE) -I$(DEVICE_INCLUDE) -I$(CMSIS_INCLUDE)
CFLAGS += -I$(PROJECT_ROOT)/inc -I$(PROJECT_ROOT)/src

# 源文件
SOURCES = main.c stm32_flash.c

# 目标文件
OBJECTS = $(SOURCES:.c=.o)

# 输出文件
TARGET = flash_kv_demo.elf
BIN = flash_kv_demo.bin

# 默认目标
all: $(BIN)

# 链接
$(TARGET): $(OBJECTS) $(KV_LIB)
	$(CC) -T stm32f407vg.ld $(OBJECTS) $(KV_LIB) -o $@ -lc -lm -lnosys $(CFLAGS)
	$(SIZE) $@

# 编译
%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

# 生成bin文件
$(BIN): $(TARGET)
	$(OBJCOPY) -O binary $< $@

# 清理
clean:
	rm -f $(OBJECTS) $(TARGET) $(BIN)

# 烧录 (使用ST-Link)
flash: $(BIN)
	st-flash write $(BIN) 0x08000000

# 打开串口
monitor:
	minicom -D /dev/ttyUSB0 -b 115200

.PHONY: all clean flash monitor
