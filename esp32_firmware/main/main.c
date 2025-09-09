#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "network/wifi_provisioning.h"
#include "network/mdns_service.h"
#include "audio/audio_pipeline_manager.h"
#include "peripheral/peripheral_manager.h"
#include "control/device_manager.h"
#include "video/video_manager.h"

static const char *TAG = "UDP_AUDIO_MAIN";

void app_main(void) {
    esp_log_level_set("udp_STREAM", ESP_LOG_DEBUG);

    // Initialize NVS with error handling
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "NVS partition issues detected, erasing and retrying...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");
    
    // Add a small delay to ensure NVS is fully ready
    vTaskDelay(pdMS_TO_TICKS(100));

    // Initialize peripheral manager
    ESP_LOGI(TAG, "Initializing peripheral manager...");
    ESP_ERROR_CHECK(peripheral_manager_init());

    // Start WiFi provisioning
    ESP_LOGI(TAG, "Starting WiFi provisioning...");
    start_wifi_provisioning();
    
    ESP_LOGI(TAG, "WiFi connected successfully!");

    // Initialize mDNS service
    ESP_LOGI(TAG, "Initializing mDNS service..."); 
    ESP_ERROR_CHECK(mdns_service_init());
    // Add TCP service for device control (port 12345)
    ESP_ERROR_CHECK(mdns_add_tcp_service(12345));
    
    // Set log levels
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_DEBUG);

    // Initialize video manager
    ESP_LOGI(TAG, "Initializing video manager...");
    esp_err_t video_ret = video_manager_init();
    if (video_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize video manager: %s", esp_err_to_name(video_ret));
        // Continue without video functionality
    } else {
        ESP_LOGI(TAG, "Video manager initialized successfully");
    }
    // Start device manager task
    device_manager_init();

    // Main loop can be empty as tasks handle the work
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

}
