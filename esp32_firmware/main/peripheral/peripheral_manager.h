#ifndef PERIPHERAL_MANAGER_H
#define PERIPHERAL_MANAGER_H

#include "esp_err.h"
#include "esp_peripherals.h"
#include "input_key_service.h"

#define BUTTON_PRESS_DURATION_MS 3000  // 3 seconds for long press
#define WAIT_TIME_BETWEEN_NOTIFICATIONS_MS 5000  // 5 seconds between notifications

/**
 * @brief Initialize the peripheral management system
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t peripheral_manager_init(void);

/**
 * @brief Clear WiFi provisioning and restart device
 */
void trigger_wifi_reset(void);

#endif // PERIPHERAL_MANAGER_H
