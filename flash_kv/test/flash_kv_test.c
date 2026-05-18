/**
 * @file flash_kv_test.c
 * @brief Flash KV 单元测试
 * @description 覆盖基础操作、类型转换、GC、事务、双区域备份、哈希删除等功能
 * @author EasyData
 * @date 2026-02-25
 * @version 1.1.0
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "flash_kv.h"
#include "flash_kv_utils.h"

extern const flash_kv_ops_t mock_flash_ops;
extern int mock_flash_reset(void);

static int g_test_pass = 0;
static int g_test_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
        g_test_fail++; \
        return; \
    } \
} while(0)

/* 全局初始化（每次测试前重新初始化） */
static void ensure_initialized(void)
{
    mock_flash_reset();
    flash_kv_adapter_register(&mock_flash_ops);

    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    flash_kv_init(0, &config);
}

/*============================================================================
 * 基础操作测试
 *============================================================================*/

void test_kv_set_get(void)
{
    printf("\n  [Test] KV Set/Get Basic Operations\n");
    ensure_initialized();

    /* 写入键值对 */
    const uint8_t key1[] = "device_name";
    const uint8_t value1[] = "sensor_001";
    int ret = flash_kv_set(key1, sizeof(key1) - 1, value1, sizeof(value1) - 1);
    TEST_ASSERT(ret == KV_OK, "set should succeed");

    /* 读取验证 */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key1, sizeof(key1) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "get should succeed");
    TEST_ASSERT(len == sizeof(value1) - 1, "value length should match");
    TEST_ASSERT(memcmp(read_val, value1, len) == 0, "value should match");

    /* 存在检查 */
    TEST_ASSERT(flash_kv_exists(key1, sizeof(key1) - 1) == true, "key should exist");

    /* 多种类型数据 */
    const uint8_t key_num[] = "counter";
    uint8_t buf_num[4];
    kv_put_u32le(buf_num, 12345);
    flash_kv_set(key_num, sizeof(key_num) - 1, buf_num, sizeof(buf_num));

    uint8_t read_num[64];
    len = sizeof(read_num);
    flash_kv_get(key_num, sizeof(key_num) - 1, read_num, &len);
    TEST_ASSERT(kv_get_u32le(read_num) == 12345, "u32 value should match");

    /* float */
    const uint8_t key_float[] = "temperature";
    uint8_t buf_float[4];
    kv_put_float(buf_float, 25.5f);
    flash_kv_set(key_float, sizeof(key_float) - 1, buf_float, sizeof(buf_float));

    uint8_t read_float[64];
    len = sizeof(read_float);
    flash_kv_get(key_float, sizeof(key_float) - 1, read_float, &len);
    TEST_ASSERT(kv_get_float(read_float) > 25.4f && kv_get_float(read_float) < 25.6f,
                "float value should match");

    printf("  [PASS] Basic Operations Test\n");
    g_test_pass++;
}

void test_kv_update(void)
{
    printf("\n  [Test] KV Update (Same Key, Different Value)\n");
    ensure_initialized();

    const uint8_t key[] = "firmware_version";
    const uint8_t value1[] = "v1.0.0";
    const uint8_t value2[] = "v1.0.1";
    const uint8_t value3[] = "v2.0.0";

    flash_kv_set(key, sizeof(key) - 1, value1, sizeof(value1) - 1);
    flash_kv_set(key, sizeof(key) - 1, value2, sizeof(value2) - 1);
    flash_kv_set(key, sizeof(key) - 1, value3, sizeof(value3) - 1);

    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    int ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "get after update should succeed");
    TEST_ASSERT(memcmp(read_val, value3, sizeof(value3) - 1) == 0,
                "should get latest value");

    /* 更新后record_count应仍为1 (同一个key) */
    uint32_t count = flash_kv_count();
    TEST_ASSERT(count == 1, "count should be 1 after updating same key");

    printf("  [PASS] Update Test\n");
    g_test_pass++;
}

void test_kv_delete(void)
{
    printf("\n  [Test] KV Delete\n");
    ensure_initialized();

    const uint8_t key[] = "temp_key";
    const uint8_t value[] = "to_be_deleted";

    flash_kv_set(key, sizeof(key) - 1, value, sizeof(value) - 1);
    TEST_ASSERT(flash_kv_exists(key, sizeof(key) - 1) == true, "key should exist before delete");

    int ret = flash_kv_del(key, sizeof(key) - 1);
    TEST_ASSERT(ret == KV_OK, "delete should succeed");
    TEST_ASSERT(flash_kv_exists(key, sizeof(key) - 1) == false, "key should not exist after delete");

    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_ERR_NOT_FOUND, "get after delete should return NOT_FOUND");

    printf("  [PASS] Delete Test\n");
    g_test_pass++;
}

/*============================================================================
 * 哈希表删除后探测链测试 (Bug #4 修复验证)
 *============================================================================*/

void test_kv_delete_probe_chain(void)
{
    printf("\n  [Test] KV Delete Probe Chain (Tombstone)\n");
    ensure_initialized();

    /*
     * 构造场景: 多个key哈希到相同桶, 删除中间的key后, 后面的key仍可找到
     * 使用大量key来增加碰撞概率
     */
    const uint8_t key_a[] = "key_alpha";
    const uint8_t key_b[] = "key_beta";
    const uint8_t key_c[] = "key_gamma";
    const uint8_t val_a[] = "val_a";
    const uint8_t val_b[] = "val_b";
    const uint8_t val_c[] = "val_c";

    flash_kv_set(key_a, sizeof(key_a) - 1, val_a, sizeof(val_a) - 1);
    flash_kv_set(key_b, sizeof(key_b) - 1, val_b, sizeof(val_b) - 1);
    flash_kv_set(key_c, sizeof(key_c) - 1, val_c, sizeof(val_c) - 1);

    /* 删除中间的key */
    int ret = flash_kv_del(key_b, sizeof(key_b) - 1);
    TEST_ASSERT(ret == KV_OK, "delete key_b should succeed");

    /* 验证其他key仍可正常读取 */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key_a, sizeof(key_a) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "key_a should still be readable after key_b delete");
    TEST_ASSERT(memcmp(read_val, val_a, sizeof(val_a) - 1) == 0, "key_a value should match");

    len = sizeof(read_val);
    ret = flash_kv_get(key_c, sizeof(key_c) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "key_c should still be readable after key_b delete");
    TEST_ASSERT(memcmp(read_val, val_c, sizeof(val_c) - 1) == 0, "key_c value should match");

    /* 删除的key不应存在 */
    TEST_ASSERT(flash_kv_exists(key_b, sizeof(key_b) - 1) == false,
                "key_b should not exist after delete");

    /* 压力测试: 写入20个key, 删除奇数位, 验证偶数位 */
    ensure_initialized();
    char key_buf[32], val_buf[64];
    for (int i = 0; i < 20; i++) {
        snprintf(key_buf, sizeof(key_buf), "probe_%02d", i);
        snprintf(val_buf, sizeof(val_buf), "value_%02d", i);
        flash_kv_set((uint8_t *)key_buf, strlen(key_buf),
                     (uint8_t *)val_buf, strlen(val_buf));
    }

    /* 删除奇数位 */
    for (int i = 1; i < 20; i += 2) {
        snprintf(key_buf, sizeof(key_buf), "probe_%02d", i);
        flash_kv_del((uint8_t *)key_buf, strlen(key_buf));
    }

    /* 验证偶数位仍然存在 */
    for (int i = 0; i < 20; i += 2) {
        snprintf(key_buf, sizeof(key_buf), "probe_%02d", i);
        snprintf(val_buf, sizeof(val_buf), "value_%02d", i);
        len = sizeof(read_val);
        ret = flash_kv_get((uint8_t *)key_buf, strlen(key_buf), read_val, &len);
        TEST_ASSERT(ret == KV_OK, "even keys should still be readable");
        TEST_ASSERT(memcmp(read_val, val_buf, strlen(val_buf)) == 0, "even key values should match");
    }

    /* 验证奇数位已删除 */
    for (int i = 1; i < 20; i += 2) {
        snprintf(key_buf, sizeof(key_buf), "probe_%02d", i);
        TEST_ASSERT(flash_kv_exists((uint8_t *)key_buf, strlen(key_buf)) == false,
                    "odd keys should not exist");
    }

    printf("  [PASS] Delete Probe Chain Test\n");
    g_test_pass++;
}

/*============================================================================
 * Flash兼容性测试 (Bug #3 修复验证)
 *============================================================================*/

void test_kv_flash_flags(void)
{
    printf("\n  [Test] KV Flash-Compatible Flags\n");
    ensure_initialized();

    const uint8_t key[] = "flash_test";
    const uint8_t value1[] = "original";
    const uint8_t value2[] = "updated";

    /* 写入 */
    flash_kv_set(key, sizeof(key) - 1, value1, sizeof(value1) - 1);

    /* 更新 (旧记录flags从VALID→DELETED, 只需bit清零) */
    flash_kv_set(key, sizeof(key) - 1, value2, sizeof(value2) - 1);

    /* 验证能读到新值 */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    int ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "get after update should succeed");
    TEST_ASSERT(memcmp(read_val, value2, sizeof(value2) - 1) == 0,
                "should get updated value");

    /* 验证重新init后数据持久化 */
    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    flash_kv_init(0, &config);

    len = sizeof(read_val);
    ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "get after re-init should succeed");
    TEST_ASSERT(memcmp(read_val, value2, sizeof(value2) - 1) == 0,
                "value should persist after re-init");

    /* count应为1 */
    TEST_ASSERT(flash_kv_count() == 1, "count should be 1 after re-init");

    printf("  [PASS] Flash Flags Test\n");
    g_test_pass++;
}

/*============================================================================
 * GC测试 (Bug #7 修复验证)
 *============================================================================*/

void test_kv_gc(void)
{
    printf("\n  [Test] KV Garbage Collection\n");
    ensure_initialized();

    /* 写入一些数据 */
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "gc_key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);
        int r = flash_kv_set((uint8_t *)key, strlen(key),
                             (uint8_t *)value, strlen(value));
        TEST_ASSERT(r == KV_OK, "set during GC test should succeed");
    }

    uint32_t count_before = flash_kv_count();
    TEST_ASSERT(count_before == 5, "should have 5 records");

    /* 删除一些记录以产生垃圾 */
    flash_kv_del((uint8_t *)"gc_key_0", 8);
    flash_kv_del((uint8_t *)"gc_key_2", 8);

    TEST_ASSERT(flash_kv_count() == 3, "should have 3 records after delete");

    /* 手动触发GC */
    int ret = flash_kv_gc();
    TEST_ASSERT(ret == KV_OK, "GC should succeed");
    TEST_ASSERT(flash_kv_count() == 3, "should still have 3 records after GC");

    /* 验证数据完整性 */
    uint8_t value[64];
    uint8_t len;

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"gc_key_1", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "gc_key_1 should survive GC");
    TEST_ASSERT(memcmp(value, "value_1", 7) == 0, "gc_key_1 value should match");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"gc_key_3", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "gc_key_3 should survive GC");
    TEST_ASSERT(memcmp(value, "value_3", 7) == 0, "gc_key_3 value should match");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"gc_key_4", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "gc_key_4 should survive GC");

    /* 已删除的key不应存在 */
    TEST_ASSERT(flash_kv_exists((uint8_t *)"gc_key_0", 8) == false,
                "gc_key_0 should not survive GC");
    TEST_ASSERT(flash_kv_exists((uint8_t *)"gc_key_2", 8) == false,
                "gc_key_2 should not survive GC");

    /* GC后仍能写入新数据 */
    ret = flash_kv_set((uint8_t *)"new_after_gc", 12,
                       (uint8_t *)"new_value", 9);
    TEST_ASSERT(ret == KV_OK, "should be able to write after GC");

    printf("  [PASS] GC Test\n");
    g_test_pass++;
}

/*============================================================================
 * GC后重新初始化测试 (验证header持久化)
 *============================================================================*/

void test_kv_gc_persistence(void)
{
    printf("\n  [Test] KV GC Persistence (Header Write)\n");
    ensure_initialized();

    /* 写入数据, 执行GC, 然后re-init */
    flash_kv_set((uint8_t *)"persist_1", 9, (uint8_t *)"data_1", 6);
    flash_kv_set((uint8_t *)"persist_2", 9, (uint8_t *)"data_2", 6);
    flash_kv_set((uint8_t *)"persist_3", 9, (uint8_t *)"data_3", 6);

    /* 删除一个, 触发GC */
    flash_kv_del((uint8_t *)"persist_2", 9);
    int ret = flash_kv_gc();
    TEST_ASSERT(ret == KV_OK, "GC should succeed");

    /* 重新初始化 (模拟重启) */
    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    flash_kv_init(0, &config);

    /* 验证数据在GC+重启后仍存在 */
    uint8_t value[64];
    uint8_t len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"persist_1", 9, value, &len);
    TEST_ASSERT(ret == KV_OK, "persist_1 should survive GC + reinit");
    TEST_ASSERT(memcmp(value, "data_1", 6) == 0, "persist_1 value should match");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"persist_3", 9, value, &len);
    TEST_ASSERT(ret == KV_OK, "persist_3 should survive GC + reinit");

    TEST_ASSERT(flash_kv_exists((uint8_t *)"persist_2", 9) == false,
                "persist_2 should not survive GC + reinit");

    TEST_ASSERT(flash_kv_count() == 2, "count should be 2 after GC + reinit");

    printf("  [PASS] GC Persistence Test\n");
    g_test_pass++;
}

/*============================================================================
 * 事务测试 (多记录事务)
 *============================================================================*/

void test_kv_transaction(void)
{
    printf("\n  [Test] KV Transaction\n");
    ensure_initialized();

    /* 测试事务提交 */
    int ret = flash_kv_tx_begin();
    TEST_ASSERT(ret == KV_OK, "tx_begin should succeed");

    /* 事务内写入多条 */
    flash_kv_set((uint8_t *)"tx_key_1", 8, (uint8_t *)"tx_val_1", 8);
    flash_kv_set((uint8_t *)"tx_key_2", 8, (uint8_t *)"tx_val_2", 8);
    flash_kv_set((uint8_t *)"tx_key_3", 8, (uint8_t *)"tx_val_3", 8);

    /* 提交前, 数据在缓冲区中, Flash中没有 (但通过事务get可以读到) */
    uint8_t value[64];
    uint8_t len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"tx_key_1", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "should be able to read tx buffer");

    /* 提交 */
    ret = flash_kv_tx_commit();
    TEST_ASSERT(ret == KV_OK, "tx_commit should succeed");

    /* 提交后数据应持久化 */
    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"tx_key_1", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "tx_key_1 should exist after commit");
    TEST_ASSERT(memcmp(value, "tx_val_1", 8) == 0, "tx_key_1 value should match");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"tx_key_2", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "tx_key_2 should exist after commit");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"tx_key_3", 8, value, &len);
    TEST_ASSERT(ret == KV_OK, "tx_key_3 should exist after commit");

    TEST_ASSERT(flash_kv_count() == 3, "should have 3 records after commit");

    printf("  [PASS] Transaction Commit Test\n");
    g_test_pass++;
}

void test_kv_transaction_rollback(void)
{
    printf("\n  [Test] KV Transaction Rollback\n");
    ensure_initialized();

    /* 先写入一个基准值 */
    flash_kv_set((uint8_t *)"base_key", 8, (uint8_t *)"base_val", 8);

    /* 开始事务 */
    int ret = flash_kv_tx_begin();
    TEST_ASSERT(ret == KV_OK, "tx_begin should succeed");

    /* 事务内写入 */
    flash_kv_set((uint8_t *)"rollback_1", 10, (uint8_t *)"rb_val_1", 8);
    flash_kv_set((uint8_t *)"rollback_2", 10, (uint8_t *)"rb_val_2", 8);

    /* 回滚 */
    ret = flash_kv_tx_rollback();
    TEST_ASSERT(ret == KV_OK, "tx_rollback should succeed");

    /* 回滚后, 事务内的写入应该不存在 */
    TEST_ASSERT(flash_kv_exists((uint8_t *)"rollback_1", 10) == false,
                "rollback_1 should not exist after rollback");
    TEST_ASSERT(flash_kv_exists((uint8_t *)"rollback_2", 10) == false,
                "rollback_2 should not exist after rollback");

    /* 基准值应仍然存在 */
    TEST_ASSERT(flash_kv_exists((uint8_t *)"base_key", 8) == true,
                "base_key should still exist");

    printf("  [PASS] Transaction Rollback Test\n");
    g_test_pass++;
}

/*============================================================================
 * 双区域测试
 *============================================================================*/

void test_kv_dual_region(void)
{
    printf("\n  [Test] KV Dual Region Backup\n");
    ensure_initialized();

    kv_handle_t *handle = flash_kv_get_handle(0);
    TEST_ASSERT(handle != NULL, "handle should not be NULL");

    uint32_t active_before = handle->active_region;

    /* 写入数据 */
    flash_kv_set((uint8_t *)"region_key", 10, (uint8_t *)"region_val", 10);

    /* GC触发区域切换 */
    int ret = flash_kv_gc();
    TEST_ASSERT(ret == KV_OK, "GC should succeed");

    uint32_t active_after = handle->active_region;
    TEST_ASSERT(active_after != active_before, "region should switch after GC");

    /* 数据应在新区域中存在 */
    uint8_t value[64];
    uint8_t len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"region_key", 10, value, &len);
    TEST_ASSERT(ret == KV_OK, "data should exist in new region");
    TEST_ASSERT(memcmp(value, "region_val", 10) == 0, "value should match");

    printf("  [PASS] Dual Region Test\n");
    g_test_pass++;
}

/*============================================================================
 * Clear测试 (Bug #9 修复验证)
 *============================================================================*/

void test_kv_clear(void)
{
    printf("\n  [Test] KV Clear\n");
    ensure_initialized();

    /* 写入数据 */
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "clear_key_%d", i);
        snprintf(value, sizeof(value), "clear_val_%d", i);
        flash_kv_set((uint8_t *)key, strlen(key), (uint8_t *)value, strlen(value));
    }
    TEST_ASSERT(flash_kv_count() == 5, "should have 5 before clear");

    /* 清除 */
    int ret = flash_kv_clear();
    TEST_ASSERT(ret == KV_OK, "clear should succeed");
    TEST_ASSERT(flash_kv_count() == 0, "should have 0 after clear");

    /* clear后应能继续写入 */
    ret = flash_kv_set((uint8_t *)"after_clear", 11,
                       (uint8_t *)"new_val", 7);
    TEST_ASSERT(ret == KV_OK, "write after clear should succeed");

    uint8_t value[64];
    uint8_t len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"after_clear", 11, value, &len);
    TEST_ASSERT(ret == KV_OK, "read after clear+write should succeed");
    TEST_ASSERT(memcmp(value, "new_val", 7) == 0, "value should match");

    /* clear后re-init应正常 */
    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    flash_kv_init(0, &config);

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"after_clear", 11, value, &len);
    TEST_ASSERT(ret == KV_OK, "data should persist after clear + reinit");

    printf("  [PASS] Clear Test\n");
    g_test_pass++;
}

/*============================================================================
 * foreach测试 (Bug #8 修复验证)
 *============================================================================*/

static int foreach_counter = 0;
static int foreach_callback(const uint8_t *key, uint8_t key_len,
                           const uint8_t *value, uint8_t value_len,
                           void *user_data)
{
    (void)key; (void)key_len; (void)value; (void)value_len;
    int *count = (int *)user_data;
    (*count)++;
    return 0;
}

void test_kv_foreach(void)
{
    printf("\n  [Test] KV Foreach\n");
    ensure_initialized();

    /* 写入5条 */
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "foreach_%d", i);
        snprintf(value, sizeof(value), "val_%d", i);
        flash_kv_set((uint8_t *)key, strlen(key), (uint8_t *)value, strlen(value));
    }

    int count = 0;
    int ret = flash_kv_foreach(foreach_callback, &count);
    TEST_ASSERT(ret == KV_OK, "foreach should succeed");
    TEST_ASSERT(count == 5, "foreach should visit 5 records");

    /* 删除2条后 */
    flash_kv_del((uint8_t *)"foreach_1", 9);
    flash_kv_del((uint8_t *)"foreach_3", 9);

    count = 0;
    ret = flash_kv_foreach(foreach_callback, &count);
    TEST_ASSERT(ret == KV_OK, "foreach after delete should succeed");
    TEST_ASSERT(count == 3, "foreach should visit 3 records after delete");

    printf("  [PASS] Foreach Test\n");
    g_test_pass++;
}

/*============================================================================
 * 边界测试
 *============================================================================*/

void test_kv_boundary(void)
{
    printf("\n  [Test] Boundary Tests\n");
    ensure_initialized();

    /* 空key */
    int ret = flash_kv_set((uint8_t *)"", 0, (uint8_t *)"val", 3);
    TEST_ASSERT(ret == KV_ERR_INVALID_PARAM, "empty key should fail");

    /* 超长key */
    char long_key[64];
    memset(long_key, 'K', 63);
    long_key[63] = 0;
    ret = flash_kv_set((uint8_t *)long_key, 63, (uint8_t *)"val", 3);
    TEST_ASSERT(ret == KV_ERR_INVALID_PARAM, "key > 32 bytes should fail");

    /* 超长value */
    char long_val[128];
    memset(long_val, 'V', 127);
    ret = flash_kv_set((uint8_t *)"k", 1, (uint8_t *)long_val, 127);
    TEST_ASSERT(ret == KV_ERR_INVALID_PARAM, "value > 64 bytes should fail");

    /* 最大长度key (32字节) */
    char max_key[32];
    memset(max_key, 'M', 32);
    ret = flash_kv_set((uint8_t *)max_key, 32, (uint8_t *)"ok", 2);
    TEST_ASSERT(ret == KV_OK, "max length key should succeed");

    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get((uint8_t *)max_key, 32, read_val, &len);
    TEST_ASSERT(ret == KV_OK, "get max length key should succeed");
    TEST_ASSERT(memcmp(read_val, "ok", 2) == 0, "max key value should match");

    /* NULL参数 */
    ret = flash_kv_set(NULL, 1, (uint8_t *)"val", 3);
    TEST_ASSERT(ret == KV_ERR_INVALID_PARAM, "NULL key should fail");

    ret = flash_kv_get((uint8_t *)"k", 1, NULL, &len);
    TEST_ASSERT(ret == KV_ERR_INVALID_PARAM, "NULL value buf should fail");

    printf("  [PASS] Boundary Test\n");
    g_test_pass++;
}

/*============================================================================
 * 状态接口测试
 *============================================================================*/

void test_kv_status(void)
{
    printf("\n  [Test] KV Status\n");
    ensure_initialized();

    uint32_t total = 0, used = 0;
    int ret = flash_kv_status(&total, &used);
    TEST_ASSERT(ret == KV_OK, "status should succeed");
    TEST_ASSERT(total > 0, "total should be > 0");
    TEST_ASSERT(used == 0, "used should be 0 initially");

    /* 写入一些数据 */
    flash_kv_set((uint8_t *)"status_key", 10, (uint8_t *)"val", 3);

    ret = flash_kv_status(&total, &used);
    TEST_ASSERT(ret == KV_OK, "status after write should succeed");
    TEST_ASSERT(used > 0, "used should be > 0 after write");

    uint8_t free_pct = flash_kv_free_percent();
    TEST_ASSERT(free_pct > 0 && free_pct <= 100, "free percent should be valid");

    printf("  [PASS] Status Test\n");
    g_test_pass++;
}

/*============================================================================
 * 类型工具函数测试
 *============================================================================*/

void test_kv_type_utils(void)
{
    printf("\n  [Test] Type Utility Functions\n");

    uint8_t buf[16];

    kv_put_u8(buf, 0xAB);
    TEST_ASSERT(kv_get_u8(buf) == 0xAB, "u8 should match");

    kv_put_i8(buf, -50);
    TEST_ASSERT(kv_get_i8(buf) == -50, "i8 should match");

    kv_put_u16be(buf, 0x1234);
    TEST_ASSERT(kv_get_u16be(buf) == 0x1234, "u16be should match");

    kv_put_u16le(buf, 0x5678);
    TEST_ASSERT(kv_get_u16le(buf) == 0x5678, "u16le should match");

    kv_put_u32be(buf, 0x12345678);
    TEST_ASSERT(kv_get_u32be(buf) == 0x12345678, "u32be should match");

    kv_put_u32le(buf, 0x87654321);
    TEST_ASSERT(kv_get_u32le(buf) == 0x87654321, "u32le should match");

    kv_put_float(buf, 3.14159f);
    TEST_ASSERT(kv_get_float(buf) > 3.14f && kv_get_float(buf) < 3.15f, "float should match");

    kv_put_double(buf, 3.14159265358979);
    TEST_ASSERT(kv_get_double(buf) > 3.14 && kv_get_double(buf) < 3.15, "double should match");

    kv_put_bool(buf, true);
    TEST_ASSERT(kv_get_bool(buf) == true, "bool true should match");
    kv_put_bool(buf, false);
    TEST_ASSERT(kv_get_bool(buf) == false, "bool false should match");

    printf("  [PASS] Type Utils Test\n");
    g_test_pass++;
}

/*============================================================================
 * 压力测试
 *============================================================================*/

void test_kv_stress(void)
{
    printf("\n  [Test] Stress Test - Multiple KV Pairs\n");
    ensure_initialized();

    #define STRESS_KEY_COUNT 50
    char key_buf[32];
    char value_buf[64];

    /* 写入 */
    for (int i = 0; i < STRESS_KEY_COUNT; i++) {
        snprintf(key_buf, sizeof(key_buf), "stress_key_%03d", i);
        snprintf(value_buf, sizeof(value_buf), "stress_value_%04d", i * 100);
        int ret = flash_kv_set((const uint8_t *)key_buf, strlen(key_buf),
                                (const uint8_t *)value_buf, strlen(value_buf));
        TEST_ASSERT(ret == KV_OK, "stress write should succeed");
    }

    /* 验证 */
    for (int i = 0; i < STRESS_KEY_COUNT; i++) {
        snprintf(key_buf, sizeof(key_buf), "stress_key_%03d", i);
        snprintf(value_buf, sizeof(value_buf), "stress_value_%04d", i * 100);

        uint8_t read_val[64];
        uint8_t len = sizeof(read_val);
        int ret = flash_kv_get((const uint8_t *)key_buf, strlen(key_buf),
                               read_val, &len);
        TEST_ASSERT(ret == KV_OK, "stress read should succeed");
        TEST_ASSERT(memcmp(read_val, value_buf, strlen(value_buf)) == 0,
                    "stress value should match");
    }

    TEST_ASSERT(flash_kv_count() == STRESS_KEY_COUNT, "stress count should match");

    printf("  [PASS] Stress Test (%d keys)\n", STRESS_KEY_COUNT);
    g_test_pass++;
}

/*============================================================================
 * 更新后record_count一致性测试 (Bug #1 修复验证)
 *============================================================================*/

void test_kv_update_count_consistency(void)
{
    printf("\n  [Test] KV Update Count Consistency\n");
    ensure_initialized();

    /* 写入3个不同的key */
    flash_kv_set((uint8_t *)"key_a", 5, (uint8_t *)"val_a", 5);
    flash_kv_set((uint8_t *)"key_b", 5, (uint8_t *)"val_b", 5);
    flash_kv_set((uint8_t *)"key_c", 5, (uint8_t *)"val_c", 5);
    TEST_ASSERT(flash_kv_count() == 3, "should have 3 records");

    /* 反复更新key_b */
    for (int i = 0; i < 10; i++) {
        char val[32];
        snprintf(val, sizeof(val), "updated_%d", i);
        flash_kv_set((uint8_t *)"key_b", 5, (uint8_t *)val, strlen(val));
    }

    /* count应仍然是3 */
    TEST_ASSERT(flash_kv_count() == 3, "count should still be 3 after updates");

    /* 验证所有key可读 */
    uint8_t value[64];
    uint8_t len = sizeof(value);
    int ret = flash_kv_get((uint8_t *)"key_a", 5, value, &len);
    TEST_ASSERT(ret == KV_OK, "key_a should be readable");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"key_b", 5, value, &len);
    TEST_ASSERT(ret == KV_OK, "key_b should be readable");
    TEST_ASSERT(memcmp(value, "updated_9", 9) == 0, "key_b should have latest value");

    len = sizeof(value);
    ret = flash_kv_get((uint8_t *)"key_c", 5, value, &len);
    TEST_ASSERT(ret == KV_OK, "key_c should be readable");

    printf("  [PASS] Update Count Consistency Test\n");
    g_test_pass++;
}

/*============================================================================
 * Not Found测试
 *============================================================================*/

void test_kv_not_found(void)
{
    printf("\n  [Test] KV Not Found\n");
    ensure_initialized();

    uint8_t value[64];
    uint8_t len = sizeof(value);
    int ret = flash_kv_get((uint8_t *)"nonexistent", 11, value, &len);
    TEST_ASSERT(ret == KV_ERR_NOT_FOUND, "nonexistent key should return NOT_FOUND");
    TEST_ASSERT(flash_kv_exists((uint8_t *)"nonexistent", 11) == false,
                "nonexistent key should not exist");

    printf("  [PASS] Not Found Test\n");
    g_test_pass++;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    printf("========================================\n");
    printf("     Flash KV Unit Tests v1.1\n");
    printf("========================================\n");
    printf("Build: " __DATE__ " " __TIME__ "\n");

    test_kv_set_get();
    test_kv_update();
    test_kv_delete();
    test_kv_not_found();
    test_kv_type_utils();
    test_kv_boundary();
    test_kv_status();
    test_kv_clear();
    test_kv_foreach();
    test_kv_gc();
    test_kv_gc_persistence();
    test_kv_dual_region();
    test_kv_transaction();
    test_kv_transaction_rollback();
    test_kv_delete_probe_chain();
    test_kv_flash_flags();
    test_kv_update_count_consistency();
    test_kv_stress();

    printf("\n========================================\n");
    printf("  Results: %d PASSED, %d FAILED\n", g_test_pass, g_test_fail);
    printf("========================================\n");

    return g_test_fail > 0 ? 1 : 0;
}
