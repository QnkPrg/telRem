#include "mdns_service.h"
#include "esp_log.h"
#include "mdns.h"
#include <string.h>

static const char *TAG = "MDNS_SERVICE";

esp_err_t mdns_service_init(void)
{
    ESP_LOGI(TAG, "Initializing mDNS service...");
    
    // Initialize mDNS
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set hostname
    ret = mdns_hostname_set("telrem");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }
    
    // Set default instance name
    ret = mdns_instance_name_set("TelRem Audio Device");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }
    
    ESP_LOGI(TAG, "mDNS service initialized successfully");
    ESP_LOGI(TAG, "Device discoverable as: telrem.local");
    
    return ESP_OK;
}

esp_err_t mdns_add_tcp_service(uint16_t port)
{
    ESP_LOGI(TAG, "Adding TCP control service on port %d", port);
    
    esp_err_t ret = mdns_service_add("TelRem-Control", "_telrem", "_tcp", port, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCP service: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add service txt records for TCP control service
    mdns_txt_item_t tcp_txt_data[] = {
        {"version", "1.0"},
        {"device", "esp32-audio"},
        {"type", "control"},
        {"protocol", "tcp"}
    };
    
    ret = mdns_service_txt_set("_telrem", "_tcp", tcp_txt_data, 4);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set TCP service TXT records: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "TCP control service added successfully");
    return ESP_OK;
}

void mdns_service_cleanup(void)
{
    ESP_LOGI(TAG, "Cleaning up mDNS service...");
    mdns_free();
    ESP_LOGI(TAG, "mDNS service cleanup complete");
}
