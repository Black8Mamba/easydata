#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "flash_kv.h"
#include "flash_kv_utils.h"

extern const flash_kv_ops_t mock_flash_ops;

void test_kv_set_get(void)
{
    printf("Test: KV set/get... ");

    /* 初始化 */
    flash_kv_adapter_register(&mock_flash_ops);

    kv_instance_config_t config = {
        .start_addr = 0,
        .total_size = 64 * 1024,
        .block_size = 2048,
    };
    int ret = flash_kv_init(0, &config);
    assert(ret == KV_OK);

    /* 写入 */
    const uint8_t key[] = "test_key";
    const uint8_t value[] = "test_value_12345";
    ret = flash_kv_set(key, sizeof(key) - 1, value, sizeof(value) - 1);
    assert(ret == KV_OK);

    /* 读取 */
    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    assert(ret == KV_OK);
    assert(len == FLASH_KV_VALUE_SIZE);
    assert(memcmp(read_val, value, sizeof(value) - 1) == 0);

    /* 存在检查 */
    assert(flash_kv_exists(key, sizeof(key) - 1) == true);

    printf("PASS\n");
}

void test_kv_update(void)
{
    printf("Test: KV update... ");

    /* 写入相同key的不同value */
    const uint8_t key[] = "update_key";
    const uint8_t value1[] = "value_version_1";
    const uint8_t value2[] = "value_version_2";

    flash_kv_set(key, sizeof(key) - 1, value1, sizeof(value1) - 1);
    flash_kv_set(key, sizeof(key) - 1, value2, sizeof(value2) - 1);

    uint8_t read_val[64];
    uint8_t len = sizeof(read_val);
    int ret = flash_kv_get(key, sizeof(key) - 1, read_val, &len);
    assert(ret == KV_OK);
    assert(memcmp(read_val, value2, sizeof(value2) - 1) == 0);

    printf("PASS\n");
}

void test_kv_not_found(void)
{
    printf("Test: KV not found... ");

    const uint8_t key[] = "nonexistent_key";
    uint8_t value[64];
    uint8_t len = sizeof(value);

    int ret = flash_kv_get(key, sizeof(key) - 1, value, &len);
    assert(ret == KV_ERR_NOT_FOUND);

    printf("PASS\n");
}

void test_kv_type_utils(void)
{
    printf("Test: Type utils... ");

    uint8_t buf[4];

    /* uint16_t */
    kv_put_u16le(buf, 0x1234);
    assert(kv_get_u16le(buf) == 0x1234);

    /* uint32_t */
    kv_put_u32le(buf, 0x12345678);
    assert(kv_get_u32le(buf) == 0x12345678);

    /* float */
    kv_put_float(buf, 3.14159f);
    assert(kv_get_float(buf) > 3.14 && kv_get_float(buf) < 3.15);

    /* bool */
    kv_put_bool(buf, true);
    assert(kv_get_bool(buf) == true);
    kv_put_bool(buf, false);
    assert(kv_get_bool(buf) == false);

    printf("PASS\n");
}

int main(void)
{
    printf("=== Flash KV Unit Tests ===\n");

    test_kv_set_get();
    test_kv_update();
    test_kv_not_found();
    test_kv_type_utils();

    printf("\n=== All Tests PASSED ===\n");
    return 0;
}
