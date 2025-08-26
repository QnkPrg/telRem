#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

/**
 * @brief Initialize and start WiFi provisioning
 * 
 * This function handles the complete WiFi provisioning process including:
 * - Checking if device is already provisioned
 * - Starting SoftAP provisioning if needed
 * - Waiting for WiFi connection
 */
void start_wifi_provisioning(void);

/**
 * @brief Clear all WiFi provisioning data from NVS
 * 
 * This function removes all stored WiFi credentials and provisioning data,
 * then restarts the device to ensure a clean state.
 * 
 * @note NVS must be initialized before calling this function
 */
void clear_wifi_provisioning(void);

#endif // WIFI_PROVISIONING_H
