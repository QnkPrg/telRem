#ifndef MDNS_SERVICE_H
#define MDNS_SERVICE_H

#include "esp_err.h"

/**
 * @brief Initialize mDNS service
 * 
 * Initializes mDNS and sets up the device to be discoverable as "telrem"
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mdns_service_init(void);

/**
 * @brief Add TCP service to mDNS
 * 
 * Advertises TCP service for device control
 * 
 * @param port TCP server port
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mdns_add_tcp_service(uint16_t port);

/**
 * @brief Cleanup mDNS service
 * 
 * Stops mDNS service and frees resources
 */
void mdns_service_cleanup(void);

#endif // MDNS_SERVICE_H
