#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "esp_wifi.h"
#include "esp_event.h"

/* Signal Wi-Fi events on this event-group */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_CONNECT_SUCCESS_SENT BIT2
#define WIFI_PROVISIONING_DONE_BIT BIT3
/**
 * @brief WiFi credentials structure
 */
typedef struct {
    char ssid[32];
    char password[64];
} wifi_credentials_t;

/**
 * @brief Load WiFi credentials from NVS (centralized function)
 * 
 * @param credentials Pointer to wifi_credentials_t structure to fill
 * @return ESP_OK on success, error code on failure
 * @note NVS must be initialized before calling this function
 */
esp_err_t load_wifi_credentials_from_nvs(wifi_credentials_t *credentials);

/**
 * @brief Display WiFi credentials for debugging
 * 
 * This function displays stored WiFi credentials in the log for debugging purposes.
 * 
 * @param credentials Pointer to WiFi credentials structure to display
 * @note Only call this function with valid credentials loaded from NVS
 * @return true always (since credentials are assumed valid when passed)
 */
bool display_wifi_credentials(const wifi_credentials_t *credentials);

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

/**
 * @brief WiFi event handler for provisioning events
 * 
 * @param arg Event group handle
 * @param event_base Event base (WIFI_PROV_EVENT, WIFI_EVENT, IP_EVENT)
 * @param event_id Event ID
 * @param event_data Event data
 */
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

/**
 * @brief Save WiFi credentials to NVS storage
 * 
 * @note NVS must be initialized before calling this function
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t save_wifi_credentials_to_nvs(const char* ssid, const char* password);

/**
 * @brief Start HTTP provisioning server
 * 
 * @return ESP_OK on success
 */
esp_err_t start_provisioning_server(void);

/**
 * @brief Stop HTTP provisioning server
 */
void stop_provisioning_server(void);

/**
 * @brief Load WiFi credentials from NVS (centralized function)
 * 
 * @param credentials Pointer to wifi_credentials_t structure to fill
 * @return ESP_OK on success, error code on failure
 * @note NVS must be initialized before calling this function
 */
esp_err_t load_wifi_credentials_from_nvs(wifi_credentials_t *credentials);

/**
 * @brief Connect to WiFi using provided credentials
 * 
 * @param credentials Pointer to WiFi credentials structure
 * @note WiFi system must be initialized before calling this function
 */
void connect_wifi_with_credentials(wifi_credentials_t *credentials);

/**
 * @brief Start AP mode for provisioning
 */
void start_ap_mode(void);

/**
 * @brief Task to handle delayed provisioning cleanup after successful connection
 * 
 * @param pvParameters Task parameters (unused)
 */
void delayed_provisioning_cleanup(void *pvParameters);

#endif // WIFI_PROVISIONING_H
