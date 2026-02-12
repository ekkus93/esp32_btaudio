#include "unity.h"
#include "nvs_storage.h"
#include "platform_storage.h"  // For PLATFORM_ERR_STORAGE_* error codes
#include <string.h>
#include <stdbool.h>

// Mock declarations for base NVS types (used by stub functions below)
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;

/* Override wrappers from nvs_storage.c for host-side fault injection. */
static esp_err_t flash_init_seq[4];
static int flash_init_len;
static int flash_init_calls;
static esp_err_t flash_erase_result;
static int flash_erase_calls;
static esp_err_t open_result;
static int open_calls;
static esp_err_t commit_result;
static int commit_calls;

#define MAX_I32_KEYS 8
struct i32_entry {
    const char* key;
    esp_err_t err;
    int32_t val;
};
static struct i32_entry i32_entries[MAX_I32_KEYS];
static int i32_count;

/* Provide base NVS API stubs so weak wrappers in nvs_storage.c link cleanly. */
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out_handle)
{
    (void)name; (void)mode; if (out_handle) *out_handle = (nvs_handle_t)0x1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { (void)handle; }
esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value) { (void)handle; (void)key; (void)out_value; return ESP_OK; }
esp_err_t nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value) { (void)handle; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length) { (void)handle; (void)key; if (length) *length = 0; if (out_value) out_value[0] = '\0'; return ESP_OK; }
esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value) { (void)handle; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length) { (void)handle; (void)key; if (length) *length = 0; (void)out_value; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length) { (void)handle; (void)key; (void)value; (void)length; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key) { (void)handle; (void)key; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }

static void reset_state(void)
{
    flash_init_len = 0;
    flash_init_calls = 0;
    flash_erase_result = ESP_OK;
    flash_erase_calls = 0;
    open_result = ESP_OK;
    open_calls = 0;
    commit_result = ESP_OK;
    commit_calls = 0;
    memset(i32_entries, 0, sizeof(i32_entries));
    i32_count = 0;
}

static void push_flash_init(esp_err_t err)
{
    flash_init_seq[flash_init_len++] = err;
}

static void set_i32_entry(const char* key, esp_err_t err, int32_t val)
{
    for (int i = 0; i < i32_count; ++i) {
        if (strcmp(i32_entries[i].key, key) == 0) {
            i32_entries[i].err = err;
            i32_entries[i].val = val;
            return;
        }
    }
    if (i32_count < MAX_I32_KEYS) {
        i32_entries[i32_count].key = key;
        i32_entries[i32_count].err = err;
        i32_entries[i32_count].val = val;
        ++i32_count;
    }
}

esp_err_t nvs_storage_flash_init(void)
{
    if (flash_init_calls < flash_init_len) {
        return flash_init_seq[flash_init_calls++];
    }
    return (flash_init_len > 0) ? flash_init_seq[flash_init_len - 1] : ESP_OK;
}

esp_err_t nvs_storage_flash_erase(void)
{
    flash_erase_calls++;
    return flash_erase_result;
}

esp_err_t nvs_storage_open(const char* ns, platform_storage_mode_t mode, platform_storage_handle_t* h)
{
    (void)ns;
    (void)mode;
    (void)h;
    open_calls++;
    return open_result;
}

esp_err_t nvs_storage_close(platform_storage_handle_t h)
{
    (void)h;
    return ESP_OK;
}

static struct i32_entry* find_i32(const char* key)
{
    for (int i = 0; i < i32_count; ++i) {
        if (strcmp(i32_entries[i].key, key) == 0) {
            return &i32_entries[i];
        }
    }
    return NULL;
}

esp_err_t nvs_storage_get_i32(platform_storage_handle_t h, const char* key, int32_t* out)
{
    (void)h;
    struct i32_entry* e = find_i32(key);
    if (!e) {
        return PLATFORM_ERR_STORAGE_NOT_FOUND;
    }
    if (e->err == ESP_OK && out) {
        *out = e->val;
    }
    return e->err;
}

esp_err_t nvs_storage_set_i32(platform_storage_handle_t h, const char* key, int32_t val)
{
    (void)h;
    set_i32_entry(key, ESP_OK, val);
    return ESP_OK;
}

esp_err_t nvs_storage_get_str(platform_storage_handle_t h, const char* key, char* buf, size_t* len)
{
    (void)h;
    (void)key;
    if (len) *len = 0;
    if (buf && len && *len) buf[0] = '\0';
    return PLATFORM_ERR_STORAGE_NOT_FOUND;
}

esp_err_t nvs_storage_set_str(platform_storage_handle_t h, const char* key, const char* val)
{
    (void)h;
    (void)key;
    (void)val;
    return ESP_OK;
}

esp_err_t nvs_storage_get_blob(platform_storage_handle_t h, const char* key, void* buf, size_t* len)
{
    (void)h;
    (void)key;
    if (len) *len = 0;
    return PLATFORM_ERR_STORAGE_NOT_FOUND;
}

esp_err_t nvs_storage_set_blob(platform_storage_handle_t h, const char* key, const void* buf, size_t len)
{
    (void)h;
    (void)key;
    (void)buf;
    (void)len;
    return ESP_OK;
}

esp_err_t nvs_storage_erase_key(platform_storage_handle_t h, const char* key)
{
    (void)h;
    (void)key;
    return ESP_OK;
}

esp_err_t nvs_storage_commit(platform_storage_handle_t h)
{
    (void)h;
    commit_calls++;
    return commit_result;
}

void setUp(void) { reset_state(); }
void tearDown(void) {}

void test_init_recovers_from_no_free_pages(void)
{
    push_flash_init(PLATFORM_ERR_STORAGE_NO_FREE_PAGES);
    push_flash_init(ESP_OK);
    flash_erase_result = ESP_OK;

    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_init());
    TEST_ASSERT_EQUAL(2, flash_init_calls);
    TEST_ASSERT_EQUAL(1, flash_erase_calls);
}

void test_init_propagates_erase_failure(void)
{
    push_flash_init(PLATFORM_ERR_STORAGE_NO_FREE_PAGES);
    flash_erase_result = ESP_FAIL;

    TEST_ASSERT_EQUAL(ESP_FAIL, nvs_storage_init());
    TEST_ASSERT_EQUAL(1, flash_init_calls);
    TEST_ASSERT_EQUAL(1, flash_erase_calls);
}

void test_get_volume_namespace_missing(void)
{
    uint8_t vol = 42;
    open_result = PLATFORM_ERR_STORAGE_NOT_FOUND;

    TEST_ASSERT_EQUAL(PLATFORM_ERR_STORAGE_NOT_FOUND, nvs_storage_get_volume(&vol));
    TEST_ASSERT_EQUAL(42, vol);
    TEST_ASSERT_EQUAL(1, open_calls);
}

void test_i2s_pins_missing_use_defaults(void)
{
    int bclk = 7, ws = 8, din = 9, dout = 10;
    open_result = ESP_OK;
    set_i32_entry("i2s_bclk", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);
    set_i32_entry("i2s_ws", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);
    set_i32_entry("i2s_din", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);
    set_i32_entry("i2s_dout", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);

    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout));
    TEST_ASSERT_EQUAL(-1, bclk);
    TEST_ASSERT_EQUAL(-1, ws);
    TEST_ASSERT_EQUAL(-1, din);
    TEST_ASSERT_EQUAL(-1, dout);
}

void test_paired_count_missing_returns_not_found(void)
{
    int count = 99;
    open_result = ESP_OK;
    set_i32_entry("paired_count", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);

    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, nvs_storage_get_paired_count(&count));
    TEST_ASSERT_EQUAL(0, count);
}

void test_schema_upgrade_reinit(void)
{
    push_flash_init(PLATFORM_ERR_STORAGE_NEW_VERSION);
    push_flash_init(ESP_OK);
    flash_erase_result = ESP_OK;

    TEST_ASSERT_EQUAL(ESP_OK, nvs_storage_init());
    TEST_ASSERT_EQUAL(2, flash_init_calls);
    TEST_ASSERT_EQUAL(1, flash_erase_calls);
}

void test_repeated_no_free_pages_after_erase(void)
{
    push_flash_init(PLATFORM_ERR_STORAGE_NO_FREE_PAGES);
    push_flash_init(PLATFORM_ERR_STORAGE_NO_FREE_PAGES);
    flash_erase_result = ESP_OK;

    TEST_ASSERT_EQUAL(PLATFORM_ERR_STORAGE_NO_FREE_PAGES, nvs_storage_init());
    TEST_ASSERT_EQUAL(2, flash_init_calls);
    TEST_ASSERT_EQUAL(1, flash_erase_calls);
}

void test_new_version_with_erase_failure(void)
{
    push_flash_init(PLATFORM_ERR_STORAGE_NEW_VERSION);
    flash_erase_result = ESP_FAIL;

    TEST_ASSERT_EQUAL(ESP_FAIL, nvs_storage_init());
    TEST_ASSERT_EQUAL(1, flash_init_calls);
    TEST_ASSERT_EQUAL(1, flash_erase_calls);
}

void test_erase_succeeds_but_reinit_fails(void)
{
    push_flash_init(PLATFORM_ERR_STORAGE_NO_FREE_PAGES);
    push_flash_init(ESP_ERR_NO_MEM);
    flash_erase_result = ESP_OK;

    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, nvs_storage_init());
    TEST_ASSERT_EQUAL(2, flash_init_calls);
    TEST_ASSERT_EQUAL(1, flash_erase_calls);
}

void test_volume_get_open_failure(void)
{
    uint8_t vol = 50;
    open_result = ESP_ERR_NOT_FOUND;

    esp_err_t err = nvs_storage_get_volume(&vol);
    
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
    TEST_ASSERT_EQUAL(50, vol);  // unchanged on error
    TEST_ASSERT_EQUAL(1, open_calls);
}

void test_volume_get_i32_failure(void)
{
    uint8_t vol = 50;
    open_result = ESP_OK;
    set_i32_entry("volume", ESP_ERR_NOT_FOUND, 0);

    esp_err_t err = nvs_storage_get_volume(&vol);
    
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
    TEST_ASSERT_EQUAL(50, vol);  // unchanged on error
}

void test_volume_set_open_failure(void)
{
    open_result = ESP_FAIL;

    esp_err_t err = nvs_storage_set_volume(75);
    
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(1, open_calls);
    TEST_ASSERT_EQUAL(0, commit_calls);  // never reached commit
}

void test_volume_set_commit_failure(void)
{
    open_result = ESP_OK;
    commit_result = ESP_ERR_NO_MEM;

    esp_err_t err = nvs_storage_set_volume(75);
    
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);
    TEST_ASSERT_EQUAL(1, open_calls);
    TEST_ASSERT_EQUAL(1, commit_calls);
}

void test_i2s_pins_get_open_failure(void)
{
    int bclk = 5, ws = 6, din = 7, dout = 8;
    open_result = ESP_FAIL;

    esp_err_t err = nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout);
    
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(1, open_calls);
    // Values unchanged on open failure
    TEST_ASSERT_EQUAL(5, bclk);
    TEST_ASSERT_EQUAL(6, ws);
    TEST_ASSERT_EQUAL(7, din);
    TEST_ASSERT_EQUAL(8, dout);
}

void test_i2s_pins_get_partial_failure(void)
{
    int bclk = 99, ws = 99, din = 99, dout = 99;
    open_result = ESP_OK;
    set_i32_entry("i2s_bclk", ESP_OK, 26);
    set_i32_entry("i2s_ws", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);
    set_i32_entry("i2s_din", ESP_OK, 22);
    set_i32_entry("i2s_dout", PLATFORM_ERR_STORAGE_NOT_FOUND, 0);

    esp_err_t err = nvs_storage_get_i2s_pins(&bclk, &ws, &din, &dout);
    
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(26, bclk);   // found
    TEST_ASSERT_EQUAL(-1, ws);     // not found, default
    TEST_ASSERT_EQUAL(22, din);    // found
    TEST_ASSERT_EQUAL(-1, dout);   // not found, default
}

void test_i2s_pins_set_open_failure(void)
{
    open_result = ESP_ERR_NO_MEM;

    esp_err_t err = nvs_storage_set_i2s_pins(26, 25, 22, 21);
    
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, err);
    TEST_ASSERT_EQUAL(1, open_calls);
    TEST_ASSERT_EQUAL(0, commit_calls);
}

void test_i2s_pins_set_commit_failure(void)
{
    open_result = ESP_OK;
    commit_result = ESP_FAIL;

    esp_err_t err = nvs_storage_set_i2s_pins(26, 25, 22, 21);
    
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_EQUAL(1, open_calls);
    TEST_ASSERT_EQUAL(1, commit_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_recovers_from_no_free_pages);
    RUN_TEST(test_init_propagates_erase_failure);
    RUN_TEST(test_get_volume_namespace_missing);
    RUN_TEST(test_i2s_pins_missing_use_defaults);
    RUN_TEST(test_paired_count_missing_returns_not_found);
    RUN_TEST(test_schema_upgrade_reinit);
    RUN_TEST(test_repeated_no_free_pages_after_erase);
    RUN_TEST(test_new_version_with_erase_failure);
    RUN_TEST(test_erase_succeeds_but_reinit_fails);
    RUN_TEST(test_volume_get_open_failure);
    RUN_TEST(test_volume_get_i32_failure);
    RUN_TEST(test_volume_set_open_failure);
    RUN_TEST(test_volume_set_commit_failure);
    RUN_TEST(test_i2s_pins_get_open_failure);
    RUN_TEST(test_i2s_pins_get_partial_failure);
    RUN_TEST(test_i2s_pins_set_open_failure);
    RUN_TEST(test_i2s_pins_set_commit_failure);
    return UNITY_END();
}
