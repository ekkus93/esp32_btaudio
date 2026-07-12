/* bt_source_stubs_scan.c — scan, device registry + paired-device storage stubs.
 * Split out of bt_source_stubs.c; shares bt_source_stubs_internal.h. */
#include "bt_source_stubs_internal.h"

static const char *TAG = "BT_SOURCE_STUB";

static void simulate_discovery_task(void *pvParameters);
static void bt_mock_scan_timeout_task(void *pvParameters);


/**
 * @brief Add a test device for simulated discovery
 *
 * @param addr_str   Device address string in format "XX:XX:XX:XX:XX:XX"
 * @param name       Device name
 * @param type       Device type
 */
BT_WEAK_FN void bt_mock_add_test_device(const char* addr_str, const char* name, bt_device_type_t type)
{
    if (s_device_count >= MAX_TEST_DEVICES) {
        ESP_LOGE(TAG, "Cannot add more test devices, database full");
        return;
    }

    bt_device_t* device = &s_devices[s_device_count];
    
    /* Parse address */
    unsigned int addr[6];
    if (sscanf(addr_str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) != 6) {
        ESP_LOGE(TAG, "Invalid address format: %s", addr_str);
        return;
    }
    
    for (int i = 0; i < 6; i++) {
        device->addr[i] = (uint8_t)addr[i];
    }
    
    strncpy(device->name, name, sizeof(device->name) - 1);
    device->name[sizeof(device->name) - 1] = '\0';
    device->rssi = -60; /* Default RSSI */
    
    /* Set class of device based on type */
    switch (type) {
        case BT_DEVICE_TYPE_AUDIO:
            device->cod = 0x240404; /* Audio device */
            break;
        case BT_DEVICE_TYPE_PHONE:
            device->cod = 0x200404; /* Phone */
            break;
        default:
            device->cod = 0x120104; /* Computer */
            break;
    }
    
    s_device_count++;
    ESP_LOGI(TAG, "Added test device: %s, %s", addr_str, name);
}

/* Add a helper function to add mock devices */
BT_WEAK_FN esp_err_t bt_mock_add_device(const char* addr_str, const char* name, bt_device_type_t type, bool paired)
{
    if (s_device_count >= MAX_TEST_DEVICES) {
        ESP_LOGE(TAG, "Cannot add more test devices, database full");
        return ESP_ERR_NO_MEM;
    }

    bt_device_t* device = &s_devices[s_device_count];
    
    /* Parse address */
    unsigned int addr[6];
    if (sscanf(addr_str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) != 6) {
        ESP_LOGE(TAG, "Invalid address format: %s", addr_str);
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < 6; i++) {
        device->addr[i] = (uint8_t)addr[i];
    }
    
    strncpy(device->name, name, sizeof(device->name) - 1);
    device->name[sizeof(device->name) - 1] = '\0';
    device->rssi = -60; /* Default RSSI */
    device->paired = paired;
    
    /* Set class of device based on type */
    switch (type) {
        case BT_DEVICE_TYPE_AUDIO:
            device->cod = 0x240404; /* Audio device */
            break;
        case BT_DEVICE_TYPE_PHONE:
            device->cod = 0x200404; /* Phone */
            break;
        default:
            device->cod = 0x120104; /* Computer */
            break;
    }
    
    s_device_count++;
    ESP_LOGI(TAG, "Added test device: %s, %s (paired: %s)", addr_str, name, paired ? "yes" : "no");
    
    // Also add to paired devices if paired flag is set
    if (paired && s_stub_paired_device_count < MAX_PAIRED_DEVICES) {
        memcpy(&s_paired_devices[s_stub_paired_device_count++], device, sizeof(bt_device_t));
    }
    
    return ESP_OK;
}

/* Add a paired device directly
 * Only provide a local implementation when the component does NOT provide
 * prototypes/implementation. If BT_MOCK_PROVIDES_PROTOTYPES is defined the
 * authoritative component will provide bt_mock_add_paired_device and we must
 * not define a conflicting symbol here (to avoid recursion/duplicate defs).
 */
#if !defined(BT_MOCK_PROVIDES_PROTOTYPES)
BT_WEAK_FN esp_err_t bt_mock_add_paired_device(bt_device_t* device) 
{
    if (s_stub_paired_device_count >= MAX_PAIRED_DEVICES) {
        ESP_LOGE(TAG, "Cannot add more paired devices, database full");
        return ESP_ERR_NO_MEM;
    }
    
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_paired_devices[s_stub_paired_device_count++], device, sizeof(bt_device_t));
    
    ESP_LOGI(TAG, "Added paired device: %02x:%02x:%02x:%02x:%02x:%02x, %s",
             device->addr[0], device->addr[1], device->addr[2],
             device->addr[3], device->addr[4], device->addr[5],
             device->name);
    
    return ESP_OK;
}
#endif

/**
 * @brief Start BT scanning
 */
BT_WEAK_FN esp_err_t bt_scan_start(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component-level mock scan so the authoritative device list
     * is used by tests. The component provides bt_mock_start_scan and
     * bt_mock_get_scan_results.
     */
    ESP_LOGI(TAG, "Delegating bt_scan_start to bt_mock_start_scan");
    bt_mock_start_scan();
    /* Keep the scanning flag in sync with the component mock */
    s_is_scanning = bt_mock_is_scanning();
    /* Pull results into local discovered list so callbacks and getters work */
    s_device_count = bt_mock_get_scan_results(s_devices, MAX_TEST_DEVICES);
    /* Fire discovery callbacks for each device if registered */
    for (int i = 0; i < s_device_count; i++) {
        if (scan_callback) scan_callback(&s_devices[i], scan_callback_data);
    }
    return ESP_OK;
#else
    return bt_scan(10); // Default to 10 second scan
#endif
}

/**
 * @brief Start filtered BT scan
 */
BT_WEAK_FN esp_err_t bt_scan_start_filtered(bt_device_type_t device_type)
{
    // For stub, just do a regular scan
    return bt_scan_start();
}

/**
 * @brief Stop BT scanning
 */
BT_WEAK_FN esp_err_t bt_scan_stop(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component mock stop */
    if (!bt_mock_is_scanning() && !s_is_scanning) {
        ESP_LOGW(TAG, "Scan not in progress");
        return ESP_OK;
    }
    bt_mock_stop_scan();
    s_is_scanning = false;
    if (s_discovery_task_handle != NULL) {
        vTaskDelete(s_discovery_task_handle);
        s_discovery_task_handle = NULL;
    }
    ESP_LOGI(TAG, "Stopped scanning (delegated to mock)");
    return ESP_OK;
#else
    if (!s_is_scanning) {
        ESP_LOGW(TAG, "Scan not in progress");
        return ESP_OK;
    }
    
    s_is_scanning = false;
    
    if (s_discovery_task_handle != NULL) {
        vTaskDelete(s_discovery_task_handle);
        s_discovery_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Stopped scanning (stub)");
    return ESP_OK;
#endif
}

/**
 * @brief Start BT scanning with timeout
 */
BT_WEAK_FN esp_err_t bt_scan(uint32_t duration_s)
{
    if (s_is_scanning) {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting BT scanning with %" PRIu32 " second timeout (stub)", duration_s);
    s_is_scanning = true;
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component mock scan and populate local list so callers
     * of bt_get_discovered_devices / bt_get_discovered_device_count see the
     * authoritative results.
     */
    bt_mock_start_scan();
    s_is_scanning = bt_mock_is_scanning();
    s_device_count = bt_mock_get_scan_results(s_devices, MAX_TEST_DEVICES);
        xTaskCreate(bt_mock_scan_timeout_task, "bt_mock_scan_timeout", 2048, (void*)(uintptr_t)duration_s, 5, NULL);
    for (int i = 0; i < s_device_count; i++) {
        if (scan_callback) scan_callback(&s_devices[i], scan_callback_data);
    }
    /* leave s_is_scanning in sync with component until stop is called */
    return ESP_OK;
#else
    // Create a task to simulate discovery
    xTaskCreate(simulate_discovery_task, "bt_discovery", 4096, NULL, 5, &s_discovery_task_handle);
    
    // In a real implementation we'd set up a timer, but for the mock we'll rely on
    // the discovery task ending itself
    
    return ESP_OK;
#endif
}

/**
 * @brief Register discovery callback
 */
BT_WEAK_FN esp_err_t bt_register_discovery_callback(bt_discovery_cb_t callback, void *user_data)
{
    scan_callback = callback;
    scan_callback_data = user_data;
    return ESP_OK;
}

/**
 * @brief Get the number of discovered devices
 */
BT_WEAK_FN uint16_t bt_get_discovered_device_count(void)
{
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Query component mock for authoritative scan results count */
    bt_device_t tmp[MAX_TEST_DEVICES];
    int n = bt_mock_get_scan_results(tmp, MAX_TEST_DEVICES);
    return (uint16_t)n;
#else
    return (uint16_t)s_device_count;
#endif
}

/**
 * @brief Get discovered devices
 */
BT_WEAK_FN esp_err_t bt_get_discovered_devices(bt_device_t *devices, uint16_t count, uint16_t *actual_count)
{
    if (devices == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component mock authoritative scan results */
    int got = bt_mock_get_scan_results(devices, (int)count);
    if (got < 0) got = 0;
    *actual_count = (uint16_t)got;
    return ESP_OK;
#else
    uint16_t copy_count = (count < s_device_count) ? count : (uint16_t)s_device_count;
    
    for (uint16_t i = 0; i < copy_count; i++) {
        memcpy(&devices[i], &s_devices[i], sizeof(bt_device_t));
    }
    
    *actual_count = copy_count;
    return ESP_OK;
#endif
}

/**
 * @brief Check if scanning is active
 */
BT_WEAK_FN bool bt_is_scanning(void)
{
    return s_is_scanning;
}

/**
 * @brief Set auto-reconnect
 */
BT_WEAK_FN esp_err_t bt_set_auto_reconnect(bool enable)
{
    s_auto_reconnect = enable;
    return ESP_OK;
}

/**
 * Discovery task simulation
 */
static void simulate_discovery_task(void *pvParameters)
{
    /* Wait a bit before "discovering" devices */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    /* Go through devices and notify via callback */
    for (int i = 0; i < s_device_count; i++) {
        if (scan_callback != NULL) {
            ESP_LOGI(TAG, "Discovered device: %s", s_devices[i].name);
            scan_callback(&s_devices[i], scan_callback_data);
            vTaskDelay(pdMS_TO_TICKS(100)); /* Space out discoveries */
        }
    }
    
    /* Wait a bit more */
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    /* Scan is done */
    s_is_scanning = false;
    ESP_LOGI(TAG, "Device discovery complete (stub) - found %d devices", s_device_count);
    
    /* Task will delete itself */
    s_discovery_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * Task used when delegating scans to the component mock to stop the scan
 * after the requested timeout. We pass the duration in seconds as the
 * task parameter via (void*)(uintptr_t)duration_s.
 */
static void bt_mock_scan_timeout_task(void *pvParameters)
{
    uint32_t duration_s = (uint32_t)(uintptr_t)pvParameters;

    if (duration_s == 0) {
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_s * 1000));

    /* Stop component-level scan and keep local flag in sync */
    bt_mock_stop_scan();
    s_is_scanning = bt_mock_is_scanning();

    ESP_LOGI(TAG, "Delegated scan timeout expired, stopped scan after %" PRIu32 " s", duration_s);

    vTaskDelete(NULL);
}

/**
 * @brief Get paired devices
 */
BT_WEAK_FN int bt_get_paired_devices(bt_device_t* devices, int max_devices)
{
    if (devices == NULL || max_devices <= 0) {
        return 0;
    }
    
#if defined(BT_MOCK_PROVIDES_PROTOTYPES)
    /* Delegate to component authoritative implementation */
    uint16_t actual = 0;
    esp_err_t err = bt_mock_get_paired_devices(devices, (uint16_t)max_devices, &actual);
    if (err != ESP_OK) {
        return 0;
    }
    return (int)actual;
#else
    int copy_count = (max_devices < s_stub_paired_device_count) ? max_devices : s_stub_paired_device_count;

    for (int i = 0; i < copy_count; i++) {
        memcpy(&devices[i], &s_paired_devices[i], sizeof(bt_device_t));
    }

    return copy_count;
#endif
}

/**
 * @brief Store paired devices
 */
BT_WEAK_FN esp_err_t bt_store_paired_devices(void)
{
    ESP_LOGI(TAG, "Stored %d paired devices (stub)", s_stub_paired_device_count);
    return ESP_OK;
}

/**
 * @brief Load paired devices
 */
BT_WEAK_FN esp_err_t bt_load_paired_devices(void)
{
    ESP_LOGI(TAG, "Loaded paired devices (stub)");
    return ESP_OK;
}

/**
 * @brief Get paired device info
 */
BT_WEAK_FN esp_err_t bt_get_paired_device_info(const char* addr, bt_connection_info_t* info)
{
    if (addr == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < s_stub_paired_device_count; i++) {
        char dev_addr[18];
        sprintf(dev_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
                s_paired_devices[i].addr[0], s_paired_devices[i].addr[1], s_paired_devices[i].addr[2],
                s_paired_devices[i].addr[3], s_paired_devices[i].addr[4], s_paired_devices[i].addr[5]);
        
        if (strcasecmp(dev_addr, addr) == 0) {
            info->connected = (strcasecmp(dev_addr, s_connected_device_addr) == 0);
            strncpy(info->addr, dev_addr, sizeof(info->addr) - 1);
            info->addr[sizeof(info->addr) - 1] = '\0';
            strncpy(info->name, s_paired_devices[i].name, sizeof(info->name) - 1);
            info->name[sizeof(info->name) - 1] = '\0';
            info->streaming = info->connected && (s_stub_streaming_state == BT_STREAMING_STATE_STREAMING);
            info->state = info->connected ? s_stub_connection_state : BT_CONNECTION_STATE_DISCONNECTED;
            info->retry_count = 0;
            
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Check if the filter has matches
 */
BT_WEAK_FN bool bt_filter_has_matches(int device_type)
{
    /* For the stub, just check if any devices are available */
    return (s_device_count > 0);
}
