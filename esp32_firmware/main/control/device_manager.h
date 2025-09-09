#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "audio_pipeline.h"
#include "esp_event.h"

#define UDP_PORT_LOCAL      12345
#define MAX_CLIENTS         5

/**
 * @brief Initialize device management system
 * 
 * @return ESP_OK on success
 */
esp_err_t device_manager_init(void);

/**
 * @brief Broadcast doorbell ring to all connected clients
 */
void broadcast_doorbell_ring(void);

#endif // DEVICE_MANAGER_H
