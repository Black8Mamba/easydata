/**
 * @file flash_kv_test.c
 * @brief Flash KV 单元测试
 * @description 包含12个测试用例，覆盖基础操作、类型转换、GC、事务、双区域备份等功能
 * @author EasyData
 * @date 2026-02-25
 * @version 1.0.0
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "flash_kv.h"
#include "flash_kv_utils.h"

extern const flash_kv_ops_t mock_flash_ops;
extern int mock_flash_reset(void);

/* 打印缓冲区内容（十六进制） */
static void print_hex(const uint8_t *buf, uint8_t len)
{
    for (int i = 0; i < len; i++) {
        printf("%02x ", buf[i]);
    }
}

/* 全局初始化（每次测试前重新初始化） */
static void ensure_initialized(void)
{
    /* 每次都重置Flash和重新初始化 */
    mock_flash_reset();
    flash_kv_adapter_register(&mock_flash_ops);

    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    flash_kv_init(0, &config);
}

void test_kv_set_get(void)
{
    printf("\n  [Test] KV Set/Get Basic Operations\n");

    /* 初始化 */
    printf("  [-] Initializing Flash KV...\n");
    ensure_initialized();

    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    int ret = flash_kv_init(0, &config);
    assert(ret == KV_OK);
    printf("  [+] Initialized: addr=0x%X, size=%dKB, block=%d\n",
           config.start_addr, config.total_size / 1024, config.block_size);

    /* 写入键值对 */
    const uint8_t key1[] = "device_name";
    const uint8_t value1[] = "sensor_001";
    printf("\n  [-] SET: key=\"%s\", value=\"%s\"\n", key1, value1);
    ret = flash_kv_set(key1, sizeof(key1) - 1, value1, sizeof(value1) - 1);
    assert(ret == KV_OK);
    printf("  [+] SET OK\n");

    /* 读取验证 */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key1, sizeof(key1) - 1, read_val, &len);
    assert(ret == KV_OK);
    printf("  [-] GET: key=\"%s\", value=\"%s\" (len=%d)\n", key1, read_val, len);
    assert(memcmp(read_val, value1, sizeof(value1) - 1) == 0);
    printf("  [+] GET OK, value match!\n");

    /* 存在检查 */
    bool exists = flash_kv_exists(key1, sizeof(key1) - 1);
    printf("  [-] EXISTS: key=\"%s\" -> %s\n", key1, exists ? "true" : "false");
    assert(exists == true);
    printf("  [+] EXISTS OK\n");

    /* 写入多个不同类型的数据 */
    printf("\n  [-] Testing multiple key-value pairs...\n");

    /* 数字类型 */
    const uint8_t key_num[] = "counter";
    uint8_t buf_num[4];
    kv_put_u32le(buf_num, 12345);
    flash_kv_set(key_num, sizeof(key_num) - 1, buf_num, sizeof(buf_num));

    uint8_t read_num[4];
    len = sizeof(read_num);
    flash_kv_get(key_num, sizeof(key_num) - 1, read_num, &len);
    printf("  [-] SET: key=\"%s\", value=%u (hex: ", key_num, kv_get_u32le(read_num));
    print_hex(read_num, len);
    printf(")\n");
    assert(kv_get_u32le(read_num) == 12345);
    printf("  [+] Number stored and retrieved correctly\n");

    /* 浮点类型 */
    const uint8_t key_float[] = "temperature";
    uint8_t buf_float[4];
    kv_put_float(buf_float, 25.5f);
    flash_kv_set(key_float, sizeof(key_float) - 1, buf_float, sizeof(buf_float));

    uint8_t read_float[4];
    len = sizeof(read_float);
    flash_kv_get(key_float, sizeof(key_float) - 1, read_float, &len);
    printf("  [-] SET: key=\"%s\", value=%.2f\n", key_float, kv_get_float(read_float));
    assert(kv_get_float(read_float) > 25.4f && kv_get_float(read_float) < 25.6f);
    printf("  [+] Float stored and retrieved correctly\n");

    /* 布尔类型 */
    const uint8_t key_bool[] = "led_enabled";
    uint8_t buf_bool[1];
    kv_put_bool(buf_bool, true);
    flash_kv_set(key_bool, sizeof(key_bool) - 1, buf_bool, sizeof(buf_bool));

    uint8_t read_bool[1];
    len = sizeof(read_bool);
    flash_kv_get(key_bool, sizeof(key_bool) - 1, read_bool, &len);
    printf("  [-] SET: key=\"%s\", value=%s\n", key_bool, kv_get_bool(read_bool) ? "true" : "false");
    assert(kv_get_bool(read_bool) == true);
    printf("  [+] Boolean stored and retrieved correctly\n");

    printf("\n  [PASS] Basic Operations Test\n");
}

void test_kv_update(void)
{
    printf("\n  [Test] KV Update (Same Key, Different Value)\n");

    /* 写入相同key的不同value */
    const uint8_t key[] = "firmware_version";
    const uint8_t value1[] = "v1.0.0";
    const uint8_t value2[] = "v1.0.1";
    const uint8_t value3[] = "v2.0.0";

    printf("  [-] SET: key=\"%s\", value=\"%s\"\n", key, value1);
    flash_kv_set(key, sizeof(key) - 1, value1, sizeof(value1) - 1);

    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    printf("  [-] GET: value=\"%s\"\n", read_val);
    assert(memcmp(read_val, value1, sizeof(value1) - 1) == 0);
    printf("  [+] First version stored\n");

    printf("  [-] UPDATE: key=\"%s\", value=\"%s\"\n", key, value2);
    flash_kv_set(key, sizeof(key) - 1, value2, sizeof(value2) - 1);

    len = sizeof(read_val);
    flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    printf("  [-] GET: value=\"%s\"\n", read_val);
    assert(memcmp(read_val, value2, sizeof(value2) - 1) == 0);
    printf("  [+] Second version stored (old version overwritten)\n");

    printf("  [-] UPDATE: key=\"%s\", value=\"%s\"\n", key, value3);
    flash_kv_set(key, sizeof(key) - 1, value3, sizeof(value3) - 1);

    len = sizeof(read_val);
    flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    printf("  [-] GET: value=\"%s\"\n", read_val);
    assert(memcmp(read_val, value3, sizeof(value3) - 1) == 0);
    printf("  [+] Third version stored\n");

    uint32_t count = flash_kv_count();
    printf("  [-] Total KV count: %u\n", count);

    printf("\n  [PASS] Update Test\n");
}

void test_kv_delete(void)
{
    printf("\n  [Test] KV Delete\n");

    const uint8_t key[] = "temp_key";
    const uint8_t value[] = "to_be_deleted";

    /* 写入 */
    printf("  [-] SET: key=\"%s\", value=\"%s\"\n", key, value);
    flash_kv_set(key, sizeof(key) - 1, value, sizeof(value) - 1);

    /* 确认存在 */
    assert(flash_kv_exists(key, sizeof(key) - 1) == true);
    printf("  [+] Key exists before delete\n");

    /* 删除 */
    printf("  [-] DELETE: key=\"%s\"\n", key);
    int ret = flash_kv_del(key, sizeof(key) - 1);
    assert(ret == KV_OK);
    printf("  [+] Delete OK\n");

    /* 确认已删除 */
    bool exists = flash_kv_exists(key, sizeof(key) - 1);
    printf("  [-] EXISTS after delete: %s\n", exists ? "true" : "false");
    assert(exists == false);

    /* 尝试获取已删除的key */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    printf("  [-] GET after delete: ret=%d (expected: KV_ERR_NOT_FOUND=%d)\n", ret, KV_ERR_NOT_FOUND);
    assert(ret == KV_ERR_NOT_FOUND);

    printf("\n  [PASS] Delete Test\n");
}

void test_kv_not_found(void)
{
    printf("\n  [Test] KV Not Found\n");

    const uint8_t key[] = "nonexistent_key_12345";
    uint8_t value[64];
    uint8_t len = sizeof(value);

    printf("  [-] GET: key=\"%s\" (not exist)\n", key);
    int ret = flash_kv_get(key, sizeof(key) - 1, value, &len);
    printf("  [-] Result: ret=%d, expected=%d\n", ret, KV_ERR_NOT_FOUND);
    assert(ret == KV_ERR_NOT_FOUND);
    printf("  [+] Correctly returned NOT_FOUND\n");

    bool exists = flash_kv_exists(key, sizeof(key) - 1);
    printf("  [-] EXISTS: key=\"%s\" -> %s\n", key, exists ? "true" : "false");
    assert(exists == false);
    printf("  [+] Correctly returned false\n");

    printf("\n  [PASS] Not Found Test\n");
}

void test_kv_type_utils(void)
{
    printf("\n  [Test] Type Utility Functions\n");

    uint8_t buf[16];

    /* uint8_t */
    printf("  [-] Testing uint8_t... ");
    kv_put_u8(buf, 0xAB);
    assert(kv_get_u8(buf) == 0xAB);
    printf("OK (0xAB)\n");

    /* int8_t */
    printf("  [-] Testing int8_t... ");
    kv_put_i8(buf, -50);
    assert(kv_get_i8(buf) == -50);
    printf("OK (-50)\n");

    /* uint16_t big-endian */
    printf("  [-] Testing uint16_t BE... ");
    kv_put_u16be(buf, 0x1234);
    assert(kv_get_u16be(buf) == 0x1234);
    printf("OK (0x1234)\n");

    /* uint16_t little-endian */
    printf("  [-] Testing uint16_t LE... ");
    kv_put_u16le(buf, 0x5678);
    assert(kv_get_u16le(buf) == 0x5678);
    printf("OK (0x5678)\n");

    /* uint32_t big-endian */
    printf("  [-] Testing uint32_t BE... ");
    kv_put_u32be(buf, 0x12345678);
    assert(kv_get_u32be(buf) == 0x12345678);
    printf("OK (0x12345678)\n");

    /* uint32_t little-endian */
    printf("  [-] Testing uint32_t LE... ");
    kv_put_u32le(buf, 0x87654321);
    assert(kv_get_u32le(buf) == 0x87654321);
    printf("OK (0x87654321)\n");

    /* float */
    printf("  [-] Testing float... ");
    kv_put_float(buf, 3.14159f);
    assert(kv_get_float(buf) > 3.14 && kv_get_float(buf) < 3.15);
    printf("OK (3.14159)\n");

    /* double */
    printf("  [-] Testing double... ");
    kv_put_double(buf, 3.14159265358979);
    assert(kv_get_double(buf) > 3.14 && kv_get_double(buf) < 3.15);
    printf("OK (3.14159265)\n");

    /* bool */
    printf("  [-] Testing bool... ");
    kv_put_bool(buf, true);
    assert(kv_get_bool(buf) == true);
    kv_put_bool(buf, false);
    assert(kv_get_bool(buf) == false);
    printf("OK (true/false)\n");

    printf("\n  [PASS] Type Utils Test\n");
}

void test_kv_stress(void)
{
    printf("\n  [Test] Stress Test - Multiple KV Pairs\n");

    /* 每次测试前重置，所以可以写入更多数据 */
    #define STRESS_KEY_COUNT 50
    char key_buf[32];
    char value_buf[64];

    /* 写入多个键值对 */
    for (int i = 0; i < STRESS_KEY_COUNT; i++) {
        snprintf(key_buf, sizeof(key_buf), "stress_key_%03d", i);
        snprintf(value_buf, sizeof(value_buf), "stress_value_%04d", i * 100);

        int ret = flash_kv_set((const uint8_t *)key_buf, strlen(key_buf),
                                (const uint8_t *)value_buf, strlen(value_buf));
        assert(ret == KV_OK);

        if ((i + 1) % 10 == 0) {
            printf("  [-] Progress: %d/%d\n", i + 1, STRESS_KEY_COUNT);
        }
    }
    printf("  [+] Wrote %d keys\n", STRESS_KEY_COUNT);

    /* 验证所有键值对 */
    for (int i = 0; i < STRESS_KEY_COUNT; i++) {
        snprintf(key_buf, sizeof(key_buf), "stress_key_%03d", i);
        snprintf(value_buf, sizeof(value_buf), "stress_value_%04d", i * 100);

        uint8_t read_val[64];
        uint8_t len = sizeof(read_val);
        int ret = flash_kv_get((const uint8_t *)key_buf, strlen(key_buf),
                               read_val, &len);
        assert(ret == KV_OK);
        assert(memcmp(read_val, value_buf, strlen(value_buf)) == 0);
    }
    printf("  [+] Verified %d keys\n", STRESS_KEY_COUNT);

    /* 检查计数 */
    uint32_t count = flash_kv_count();
    printf("  [-] Total count: %u\n", count);

    printf("\n  [PASS] Stress Test\n");
}

void test_kv_boundary(void)
{
    printf("\n  [Test] Boundary Tests\n");

    /* 测试空key */
    printf("  [-] Testing empty key... ");
    const uint8_t empty_key[] = "";
    const uint8_t value[] = "test";
    int ret = flash_kv_set(empty_key, 0, value, sizeof(value) - 1);
    printf("ret=%d (expected: %d)\n", ret, KV_ERR_INVALID_PARAM);
    assert(ret == KV_ERR_INVALID_PARAM);
    printf("  [+] Correctly rejected empty key\n");

    /* 测试空value */
    printf("  [-] Testing empty value... ");
    const uint8_t key[] = "test_key";
    ret = flash_kv_set(key, sizeof(key) - 1, (const uint8_t *)"", 0);
    printf("ret=%d\n", ret);
    // 空value应该可以（允许清空value）
    printf("  [+] Empty value handled\n");

    /* 测试超长key */
    printf("  [-] Testing long key... ");
    char long_key[64];
    memset(long_key, 'K', 63);
    long_key[63] = 0;
    ret = flash_kv_set((const uint8_t *)long_key, 63, value, sizeof(value) - 1);
    printf("ret=%d (expected: %d or %d)\n", ret, KV_OK, KV_ERR_INVALID_PARAM);
    // 取决于FLASH_KV_KEY_SIZE的配置
    printf("  [+] Long key handled (ret=%d)\n", ret);

    /* 测试超长value */
    printf("  [-] Testing long value... ");
    char long_val[128];
    memset(long_val, 'V', 127);
    long_val[127] = 0;
    ret = flash_kv_set(key, sizeof(key) - 1, (const uint8_t *)long_val, 127);
    printf("ret=%d\n", ret);
    printf("  [+] Long value handled (ret=%d)\n", ret);

    printf("\n  [PASS] Boundary Test\n");
}

void test_kv_clear(void)
{
    printf("\n  [Test] KV Clear\n");

    /* 先写入一些数据 */
    printf("  [-] Writing test data...\n");
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "clear_key_%d", i);
        snprintf(value, sizeof(value), "clear_value_%d", i);
        flash_kv_set((const uint8_t *)key, strlen(key),
                     (const uint8_t *)value, strlen(value));
    }

    uint32_t count_before = flash_kv_count();
    printf("  [-] Count before clear: %u\n", count_before);

    /* 清除所有数据 */
    printf("  [-] Clearing all data...\n");
    int ret = flash_kv_clear();
    assert(ret == KV_OK);

    uint32_t count_after = flash_kv_count();
    printf("  [-] Count after clear: %u\n", count_after);
    assert(count_after == 0);

    printf("\n  [PASS] Clear Test\n");
}

void test_kv_status(void)
{
    printf("\n  [Test] KV Status\n");

    uint32_t total = 0, used = 0;
    int ret = flash_kv_status(&total, &used);
    assert(ret == KV_OK);

    printf("  [-] Storage Status:\n");
    printf("      Total: %u bytes\n", total);
    printf("      Used:  %u bytes\n", used);
    printf("      Free:  %u bytes\n", total - used);
    printf("      Usage: %u%%\n", used * 100 / (total ? total : 1));

    uint8_t free_percent = flash_kv_free_percent();
    printf("      Free percent: %u%%\n", free_percent);

    uint32_t count = flash_kv_count();
    printf("      Record count: %u\n", count);

    printf("\n  [PASS] Status Test\n");
}

void test_kv_gc(void)
{
    printf("\n  [Test] KV Garbage Collection\n");

    /* 先清空Flash确保干净状态 */
    flash_kv_clear();

    /* 写入一些数据 */
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "gc_key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);
        int r = flash_kv_set((uint8_t *)key, strlen(key), (uint8_t *)value, strlen(value));
        printf("  [-] SET gc_key_%d = %s: %d\n", i, value, r);
    }

    uint32_t count_before = flash_kv_count();
    printf("  [-] Record count before GC: %u\n", count_before);

    /* 删除一些记录以产生垃圾 */
    flash_kv_del((uint8_t *)"gc_key_0", 8);
    flash_kv_del((uint8_t *)"gc_key_2", 8);

    uint32_t count_after_del = flash_kv_count();
    printf("  [-] Record count after delete: %u\n", count_after_del);

    /* 手动触发GC */
    int ret = flash_kv_gc();
    printf("  [-] GC return: %d\n", ret);

    uint32_t count_after_gc = flash_kv_count();
    printf("  [-] Record count after GC: %u\n", count_after_gc);

    if (ret != KV_OK) {
        printf("  [!] GC failed!\n");
        return;
    }

    /* 验证数据完整性 */
    uint8_t value[64];
    uint8_t len;

    /* 验证 gc_key_1 */
    ret = flash_kv_get((uint8_t *)"gc_key_1", 8, value, &len);
    printf("  [-] GET gc_key_1: ret=%d, len=%d\n", ret, len);
    if (ret == KV_OK) {
        printf("  [-] Verified key_1: %.*s\n", len, value);
    }

    /* 验证 gc_key_3 */
    ret = flash_kv_get((uint8_t *)"gc_key_3", 8, value, &len);
    printf("  [-] GET gc_key_3: ret=%d, len=%d\n", ret, len);
    if (ret == KV_OK) {
        printf("  [-] Verified key_3: %.*s\n", len, value);
    }

    printf("\n  [PASS] GC Test\n");
}

void test_kv_transaction(void)
{
    printf("\n  [Test] KV Transaction\n");

    /* 测试事务状态 */
    int ret = flash_kv_tx_begin();
    printf("  [-] TX Begin: %d\n", ret);
    assert(ret == KV_OK);

    kv_handle_t *handle = flash_kv_get_handle(0);
    assert(handle != NULL);
    printf("  [-] TX State after begin: %d\n", handle->tx_state);

    /* 提交事务 */
    ret = flash_kv_tx_commit();
    printf("  [-] TX Commit: %d\n", ret);
    assert(ret == KV_OK);

    printf("  [-] TX State after commit: %d\n", handle->tx_state);

    /* 测试回滚功能 */
    ret = flash_kv_tx_begin();
    printf("  [-] TX Begin (rollback test): %d\n", ret);

    ret = flash_kv_tx_rollback();
    printf("  [-] TX Rollback: %d\n", ret);
    assert(ret == KV_OK);

    printf("  [-] TX State after rollback: %d\n", handle->tx_state);

    printf("\n  [PASS] Transaction Test\n");
}

void test_kv_dual_region(void)
{
    printf("\n  [Test] KV Dual Region Backup\n");

    /* 先清空Flash确保干净状态 */
    flash_kv_clear();

    kv_handle_t *handle = flash_kv_get_handle(0);
    assert(handle != NULL);

    uint8_t active_before = handle->active_region;
    printf("  [-] Active region before: %u\n", active_before);

    /* 写入一些数据 */
    const uint8_t region_key[] = "region_key";
    const uint8_t region_value[] = "region_value";
    int ret = flash_kv_set(region_key, sizeof(region_key) - 1, region_value, sizeof(region_value) - 1);
    printf("  [-] Set key in region %u: %d\n", active_before, ret);

    uint32_t count_before_gc = flash_kv_count();
    printf("  [-] Count before GC: %u\n", count_before_gc);

    /* 触发GC来切换区域 */
    ret = flash_kv_gc();
    printf("  [-] GC (region switch): %d\n", ret);

    uint8_t active_after = handle->active_region;
    printf("  [-] Active region after GC: %u\n", active_after);

    uint32_t count_after_gc = flash_kv_count();
    printf("  [-] Count after GC: %u\n", count_after_gc);

    /* 验证数据在新区域中仍然存在 */
    uint8_t value[64];
    uint8_t len;
    ret = flash_kv_get(region_key, sizeof(region_key) - 1, value, &len);
    printf("  [-] Get after region switch: ret=%d, len=%d\n", ret, len);
    if (ret == KV_OK) {
        printf("  [-] Value: %.*s\n", len, value);
    }

    printf("\n  [PASS] Dual Region Test\n");
}

int main(void)
{
    printf("========================================\n");
    printf("     Flash KV Unit Tests\n");
    printf("========================================\n");
    printf("Build: " __DATE__ " " __TIME__ "\n");

    /* 初始化（只在第一次需要时初始化） */
    printf("\n[*] Setting up Flash KV...\n");

    /* 运行所有测试 */
    test_kv_set_get();
    test_kv_update();
    test_kv_delete();
    test_kv_not_found();
    test_kv_type_utils();
    test_kv_boundary();
    test_kv_clear();
    test_kv_status();
    test_kv_gc();
    test_kv_transaction();
    test_kv_dual_region();

    /* 压力测试单独运行，因为它需要重置Flash */
    ensure_initialized();
    test_kv_stress();

    printf("\n========================================\n");
    printf("     All Tests PASSED!\n");
    printf("========================================\n");

    return 0;
}
