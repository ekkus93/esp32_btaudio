# ESP32-S3 WiFi Issues

## Current Status
Simple WiFi STA test on ESP32-S3 enters a reboot loop. The device shows bootloader output but the application crashes before any console output is visible.

## Symptoms
- Boot ROM: `ESP-ROM:esp32s3-20210327` visible
- After bootloader: silent reboot, no console output from app
- `idf.py build` and `idf.py flash` complete without errors

## Partition Table
```csv
nvs,      data, nvs,     ,        0x6000,
phy_init, data, phy,     ,        0x1000,
factory,  app,  factory, ,        0x400000,
```
- NVS at 0x6000-0x10000 (36KB)
- PHY init at 0x1000-0xF000 (60KB)
- Factory app at 0x10000 onwards

## Console Configuration
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` set in sdkconfig.defaults
- USB-Serial-JTAG peripheral available (SOC_USB_SERIAL_JTAG_SUPPORTED=y)
- Bootloader output works, but app output does not appear

## Code
```c
void app_main(void)
{
    ESP_LOGI(TAG, "Simple WiFi test");

    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_init_partition("nvs"));
    }

    ESP_LOGI(TAG, "Initializing WiFi...");
    esp_netif_init();
    esp_event_loop_create_default();
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_LOGI(TAG, "Starting WiFi in STA mode...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for connection...");
    while (!s_got_ip) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Still waiting...");
    }
    ESP_LOGI(TAG, "WiFi connected successfully!");
}
```

## Potential Issues
1. **USB-Serial-JTAG initialization**: App may crash before USB peripheral is ready
2. **NVS initialization**: `nvs_flash_init()` may fail silently
3. **WiFi initialization**: `esp_wifi_init()` may crash if PHY calibration data is missing
4. **Event loop**: `esp_event_loop_create_default()` is deprecated in favor of `esp_event_loop_create_default()`
5. **Missing WiFi credentials**: No SSID/password configured, so even if it boots it will just loop waiting for IP

## Build Configuration
- ESP-IDF v5.5.1
- Target: ESP32-S3
- Flash: 2MB
- Console: USB-Serial-JTAG
- SPIRAM enabled

## Flash Layout
```
0x00000   - bootloader.bin
0x01000   - wifi_simple_test.bin (factory app)
0x08000   - partition-table.bin
```

## Next Steps
1. Add explicit USB-Serial-JTAG initialization
2. Add error checking around NVS/WiFi init
3. Add explicit partition table verification
4. Check PHY calibration data
5. Add WiFi credentials or use AP mode for testing
