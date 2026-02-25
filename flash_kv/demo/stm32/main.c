/**
 * @file main.c
 * @brief Flash KV STM32 示例程序
 * @description 展示如何在STM32上使用Flash KV存储
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#include "main.h"
#include "flash_kv.h"
#include "flash_kv_config.h"
#include "stm32_flash.h"

#include <stdio.h>
#include <string.h>

/* UART用于调试输出 */
extern UART_HandleTypeDef huart1;

/**
 * @brief 系统时钟初始化
 */
static void SystemClock_Config(void);

/**
 * @brief 打印调试信息
 */
#define KV_LOG(fmt, ...)  printf("[KV] " fmt "\r\n", ##__VA_ARGS__)

/**
 * @brief 初始化Flash KV
 */
static int flash_kv_system_init(void)
{
    int ret;
    kv_instance_config_t config;

    /* 注册STM32 Flash适配器 */
    ret = flash_kv_adapter_register(&stm32_flash_ops);
    if (ret != 0) {
        KV_LOG("Failed to register flash adapter: %d", ret);
        return ret;
    }

    /* 配置KV存储区域 */
    config.start_addr = KV_FLASH_START_ADDR;
    config.total_size = KV_FLASH_SIZE;
    config.block_size = FLASH_KV_BLOCK_SIZE;
    config.ops = NULL;  /* 已通过adapter_register注册 */

    /* 初始化KV存储 */
    ret = flash_kv_init(0, &config);
    if (ret != 0) {
        KV_LOG("Failed to init flash kv: %d", ret);
        return ret;
    }

    KV_LOG("Flash KV initialized OK");
    KV_LOG("Start: 0x%08X, Size: %d bytes", config.start_addr, config.total_size);

    return 0;
}

/**
 * @brief 示例: 基本KV操作
 */
static void demo_basic_operations(void)
{
    int ret;
    uint8_t value[FLASH_KV_VALUE_SIZE];
    uint8_t value_len;
    uint8_t exists;

    KV_LOG("\r\n=== Basic Operations Demo ===");

    /* 设置键值对 */
    ret = flash_kv_set((uint8_t *)"device_name", 12,
                      (uint8_t *)"STM32F407VG", 12);
    if (ret == 0) {
        KV_LOG("Set device_name = STM32F407VG OK");
    } else {
        KV_LOG("Set device_name failed: %d", ret);
    }

    /* 读取键值对 */
    memset(value, 0, sizeof(value));
    value_len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"device_name", 12, value, &value_len);
    if (ret == 0) {
        KV_LOG("Get device_name = %s OK", (char *)value);
    } else {
        KV_LOG("Get device_name failed: %d", ret);
    }

    /* 检查键是否存在 */
    exists = flash_kv_exists((uint8_t *)"device_name", 12);
    KV_LOG("device_name exists: %s", exists ? "YES" : "NO");

    /* 删除键值对 */
    ret = flash_kv_del((uint8_t *)"device_name", 12);
    KV_LOG("Delete device_name: %s", (ret == 0) ? "OK" : "FAILED");

    /* 再次检查 */
    exists = flash_kv_exists((uint8_t *)"device_name", 12);
    KV_LOG("device_name exists after delete: %s", exists ? "YES" : "NO");
}

/**
 * @brief 示例: 存储配置参数
 */
static void demo_save_config(void)
{
    int ret;
    uint8_t value[FLASH_KV_VALUE_SIZE];
    uint8_t value_len;

    KV_LOG("\r\n=== Save Config Demo ===");

    /* 保存WiFi配置 */
    ret = flash_kv_set((uint8_t *)"wifi_ssid", 9,
                      (uint8_t *)"MyWiFiAP", 8);
    KV_LOG("Save wifi_ssid: %s", (ret == 0) ? "OK" : "FAILED");

    ret = flash_kv_set((uint8_t *)"wifi_pass", 9,
                      (uint8_t *)"password123", 10);
    KV_LOG("Save wifi_pass: %s", (ret == 0) ? "OK" : "FAILED");

    /* 保存系统配置 */
    ret = flash_kv_set((uint8_t *)"sys_baud", 8,
                      (uint8_t *)"115200", 6);
    KV_LOG("Save sys_baud: %s", (ret == 0) ? "OK" : "FAILED");

    /* 读取并显示所有配置 */
    memset(value, 0, sizeof(value));
    value_len = sizeof(value);
    if (flash_kv_get((uint8_t *)"wifi_ssid", 9, value, &value_len) == 0) {
        KV_LOG("wifi_ssid = %s", (char *)value);
    }

    memset(value, 0, sizeof(value));
    value_len = sizeof(value);
    if (flash_kv_get((uint8_t *)"sys_baud", 8, value, &value_len) == 0) {
        KV_LOG("sys_baud = %s", (char *)value);
    }
}

/**
 * @brief 示例: 事务操作
 */
static void demo_transaction(void)
{
    int ret;
    uint8_t value[FLASH_KV_VALUE_SIZE];
    uint8_t value_len;

    KV_LOG("\r\n=== Transaction Demo ===");

    /* 开始事务 */
    ret = flash_kv_tx_begin();
    KV_LOG("Tx begin: %s", (ret == 0) ? "OK" : "FAILED");

    /* 在事务中设置多个值 */
    ret = flash_kv_set((uint8_t *)"tx_key1", 7,
                       (uint8_t *)"value1", 6);
    KV_LOG("Set tx_key1 in tx: %s", (ret == 0) ? "OK" : "FAILED");

    ret = flash_kv_set((uint8_t *)"tx_key2", 7,
                       (uint8_t *)"value2", 6);
    KV_LOG("Set tx_key2 in tx: %s", (ret == 0) ? "OK" : "FAILED");

    /* 提交事务 */
    ret = flash_kv_tx_commit();
    KV_LOG("Tx commit: %s", (ret == 0) ? "OK" : "FAILED");

    /* 验证事务中的数据 */
    memset(value, 0, sizeof(value));
    value_len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"tx_key1", 7, value, &value_len);
    if (ret == 0) {
        KV_LOG("tx_key1 = %s (committed)", (char *)value);
    }

    /* 测试回滚 */
    flash_kv_tx_begin();
    flash_kv_set((uint8_t *)"tx_key3", 7, (uint8_t *)"value3", 6);
    flash_kv_tx_rollback();
    KV_LOG("Tx rollback: key3 should not exist");

    if (!flash_kv_exists((uint8_t *)"tx_key3", 7)) {
        KV_LOG("tx_key3 not exists after rollback: OK");
    }
}

/**
 * @brief 示例: 遍历所有键值对
 */
static void demo_foreach(void)
{
    int ret;

    KV_LOG("\r\n=== Foreach Demo ===");

    /* 保存一些测试数据 */
    flash_kv_set((uint8_t *)"key_a", 5, (uint8_t *)"value_a", 7);
    flash_kv_set((uint8_t *)"key_b", 5, (uint8_t *)"value_b", 7);
    flash_kv_set((uint8_t *)"key_c", 5, (uint8_t *)"value_c", 7);

    /* 遍历回调函数 */
    ret = flash_kv_foreach(NULL, NULL);  /* 仅统计数量 */
    KV_LOG("Total records: %d", flash_kv_count());
}

/**
 * @brief 示例: 垃圾回收
 */
static void demo_gc(void)
{
    int ret;
    uint8_t free_percent;

    KV_LOG("\r\n=== GC Demo ===");

    /* 触发垃圾回收 */
    ret = flash_kv_gc();
    KV_LOG("GC result: %s", (ret == 0) ? "OK" : "FAILED");

    /* 查询空闲空间百分比 */
    free_percent = flash_kv_free_percent();
    KV_LOG("Free space: %d%%", free_percent);
}

/**
 * @brief 主函数
 */
int main(void)
{
    /* HAL库初始化 */
    HAL_Init();

    /* 系统时钟配置 */
    SystemClock_Config();

    /* 初始化外设 */
    MX_GPIO_Init();
    MX_USART1_Init();

    /* 打印启动信息 */
    printf("\r\n");
    printf("========================================\r\n");
    printf("  Flash KV STM32 Demo\r\n");
    printf("  Version: 1.0.0\r\n");
    printf("========================================\r\n");

    /* 初始化Flash KV */
    if (flash_kv_system_init() != 0) {
        printf("[ERROR] Flash KV init failed!\r\n");
        while (1) {
            HAL_Delay(1000);
        }
    }

    /* 运行示例 */
    demo_basic_operations();
    demo_save_config();
    demo_transaction();
    demo_foreach();
    demo_gc();

    /* 显示状态 */
    uint32_t total, used;
    flash_kv_status(&total, &used);
    KV_LOG("\r\n=== Final Status ===");
    KV_LOG("Total: %d bytes, Used: %d bytes", total, used);
    KV_LOG("Records: %d", flash_kv_count());

    printf("\r\nDemo completed!\r\n");

    /* 主循环 */
    while (1) {
        HAL_Delay(1000);
    }
}

/**
 * @brief System Clock Configuration
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* 配置HSI为系统时钟源 */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* 配置AHB/APB时钟 */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief Error Handler
 */
void Error_Handler(void)
{
    while (1) {
        HAL_Delay(100);
    }
}

/* printf重定向到UART */
#ifdef __GNUC__
int __io_putchar(int ch)
#else
int fputc(int ch, FILE *f)
#endif
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
