#ifndef PERIPHERAL_MANAGER_H
#define PERIPHERAL_MANAGER_H

#include "esp_err.h"

/**
 * @brief Initialize the peripheral management system
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t peripheral_manager_init(void);

#endif // PERIPHERAL_MANAGER_H
