#include "wifi_provisioning.h"
#include "mdns_service.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "WIFI_PROV";
static EventGroupHandle_t wifi_event_group;
static httpd_handle_t server = NULL;
static bool provisioning_active = false;

// Custom provisioning state
typedef struct {
    char ssid[32];
    char password[64];
    bool provisioning_complete;
    bool wifi_connected;
    int connection_attempts;
    int max_connection_attempts;
    bool connection_failed;
    bool has_credentials;  // Track if we have valid credentials to connect with
} prov_state_t;

static prov_state_t current_state = {
    .max_connection_attempts = 3,
    .connection_attempts = 0,
    .connection_failed = false,
    .has_credentials = false
};

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                       int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi Station started");
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                // Ignore disconnect events if we don't have credentials yet
                if (!current_state.has_credentials) {
                    ESP_LOGD(TAG, "Ignoring STA disconnect - no credentials configured yet");
                    break;
                }
                if(current_state.wifi_connected){
                    // Lost connection reseting.
                    esp_restart();
                }
                
                current_state.wifi_connected = false;
                
                // Check if we've exceeded max attempts
                // We could also want to reconnect if we were connected before and lost connection
                if (current_state.connection_attempts >= current_state.max_connection_attempts && !current_state.wifi_connected) {
                    ESP_LOGE(TAG, "Failed to connect after %d attempts. Wrong credentials?", 
                             current_state.max_connection_attempts);
                    current_state.connection_failed = true;
                    
                    // Start provisioning mode when saved credentials fail
                    ESP_LOGI(TAG, "Connection failed - starting provisioning mode");
                    
                    // Only start provisioning if not already active
                    if (!provisioning_active) {
                        // Start AP mode for provisioning
                        start_ap_mode();
                        
                        // Start provisioning server
                        start_provisioning_server();
                        
                        ESP_LOGI(TAG, "Connect to this AP and go to http://192.168.4.1 for provisioning");
                    } else {
                        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                        ESP_LOGI(TAG, "Provisioning server already active - credentials failed");
                    }
                } else {
                    current_state.connection_attempts++;
                    ESP_LOGI(TAG, "Retrying connection (%d/%d)...", 
                             current_state.connection_attempts, current_state.max_connection_attempts);
                    esp_wifi_connect();
                }
                break;
            }
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WiFi AP started - Provisioning mode active");
                break;
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "WiFi AP stopped");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        current_state.wifi_connected = true;
        current_state.provisioning_complete = true;
        
        // Save credentials to NVS
        save_wifi_credentials_to_nvs(current_state.ssid, current_state.password);
        
        // Keep AP active for a few seconds to allow client to get success notification
        if (server) {
            ESP_LOGI(TAG, "WiFi connected successfully - keeping AP active for client notification");
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
        // Create a task to stop the provisioning server after delay
        xTaskCreate(delayed_provisioning_cleanup, "prov_cleanup", 2048, NULL, 5, NULL);
    }
}

// Load WiFi credentials from NVS (centralized function)
esp_err_t load_wifi_credentials_from_nvs(wifi_credentials_t *credentials)
{
    size_t ssid_len;
    size_t password_len;
    if (!credentials) {
        ESP_LOGE(TAG, "Invalid credentials pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize credentials
    memset(credentials, 0, sizeof(wifi_credentials_t));
    
    ESP_LOGI(TAG, "Attempting to load WiFi credentials from NVS...");
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("wifi_cred", NVS_READONLY, &nvs_handle);
    if (ret == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "NVS not initialized! Call nvs_flash_init() first");
        return ESP_ERR_NVS_NOT_INITIALIZED;
    }
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No WiFi credentials found - wifi_cred namespace does not exist");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'wifi_cred': %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "NVS namespace 'wifi_cred' opened successfully");

    // Get SSID length first
    ret = nvs_get_blob(nvs_handle, "sta.ssid", NULL, &ssid_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No WiFi SSID found in storage");
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SSID length: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    if (ssid_len == 0) {
        ESP_LOGI(TAG, "WiFi SSID found but length is 0");
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }

    // Check if SSID length is too large for our buffer
    if (ssid_len >= sizeof(credentials->ssid)) {
        ESP_LOGE(TAG, "SSID too long: %d bytes (max %d)", ssid_len, sizeof(credentials->ssid) - 1);
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    ret = nvs_get_blob(nvs_handle, "sta.ssid", credentials->ssid, &ssid_len);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No WiFi SSID found in storage: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Null-terminate the SSID since ESP32 stores it without null terminator
    credentials->ssid[ssid_len] = '\0';
    
    ESP_LOGI(TAG, "WiFi SSID loaded successfully: length=%d", ssid_len);

    // Get password length first
    ret = nvs_get_blob(nvs_handle, "sta.pswd", NULL, &password_len);
    if (ret == ESP_ERR_NVS_NOT_FOUND || password_len == 0) {
        ESP_LOGI(TAG, "No WiFi password found in storage - assuming open network");
        credentials->password[0] = '\0'; // Open network
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi credentials loaded: SSID=%s, Password=(none)", credentials->ssid);
        return ESP_OK;
    }
    
    // Check if password length is too large for our buffer
    if (password_len >= sizeof(credentials->password)) {
        ESP_LOGE(TAG, "Password too long: %d bytes (max %d)", password_len, sizeof(credentials->password) - 1);
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }
    
    esp_err_t pwd_ret = nvs_get_blob(nvs_handle, "sta.pswd", credentials->password, &password_len);
    if (pwd_ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to load password: %s", esp_err_to_name(pwd_ret));
        credentials->password[0] = '\0'; // Open network
    } else {
        // Null-terminate the password since ESP32 stores it without null terminator
        credentials->password[password_len] = '\0';
    }
    
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "WiFi credentials loaded: SSID=%s, Password=%s", 
             credentials->ssid, (strlen(credentials->password) > 0) ? "***" : "(none)");
    
    return ESP_OK;
}

// Save WiFi credentials to NVS
esp_err_t save_wifi_credentials_to_nvs(const char* ssid, const char* password)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("wifi_cred", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "NVS not initialized! Call nvs_flash_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_set_blob(nvs_handle, "sta.ssid", ssid, strlen(ssid));
    if (ret == ESP_OK && password && strlen(password) > 0) {
        ret = nvs_set_blob(nvs_handle, "sta.pswd", password, strlen(password));
    }
    
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return ret;
}

// Task to handle delayed provisioning cleanup after successful connection
void delayed_provisioning_cleanup(void *pvParameters)
{
    // Wait for the connection success event to be set
    // This ensures we only clean up after the client has received the success notification
    if(server != NULL) {
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECT_SUCCESS_SENT, pdTRUE, pdFALSE, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Cleaning up provisioning - stopping server and switching to STA mode");
    
    // Stop provisioning server
    if (server) {
        stop_provisioning_server();
    }
    
    // Switch to STA-only mode
    ESP_LOGI(TAG, "Switching to STA-only mode");
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    // Signal main application to continue execution
    xEventGroupSetBits(wifi_event_group, WIFI_PROVISIONING_DONE_BIT);

    // Clear provisioning state
    current_state.provisioning_complete = false;

    ESP_LOGI(TAG, "Provisioning cleanup complete");
    
    // Delete this task
    vTaskDelete(NULL);
}

// HTTP handler for WiFi configuration
esp_err_t config_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = malloc(total_len + 1);
    int received = 0;
    
    if (total_len >= 1024) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Request too large");
        free(buf);
        return ESP_FAIL;
    }

    // In case the cur_len is less than total_len, we need to receive the remaining data
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            free(buf);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    
    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    free(buf);
    
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");
    
    if (!cJSON_IsString(ssid_json)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid SSID");
        return ESP_FAIL;
    }
    
    // Store credentials and reset connection state
    strncpy(current_state.ssid, ssid_json->valuestring, sizeof(current_state.ssid) - 1);
    if (cJSON_IsString(password_json)) {
        strncpy(current_state.password, password_json->valuestring, sizeof(current_state.password) - 1);
    } else {
        current_state.password[0] = '\0'; // Open network
    }
    
    // Reset connection state for new attempt
    current_state.connection_attempts = 0;
    current_state.connection_failed = false;
    current_state.wifi_connected = false;
    current_state.has_credentials = true;  // Mark that we now have credentials

    ESP_LOGI(TAG, "Received WiFi credentials: SSID=%s, Password=%s", current_state.ssid, current_state.password);

    // Connect to WiFi (STA interface)
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, current_state.ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (strlen(current_state.password) > 0) {
        strncpy((char*)wifi_config.sta.password, current_state.password, sizeof(wifi_config.sta.password) - 1);
    }
    
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);


    //Start connection
    esp_err_t wifi_ret = esp_wifi_connect();
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(wifi_ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to connect to WiFi");
        return ESP_FAIL;
    }

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (xEventGroupGetBits(wifi_event_group) & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to connect to WiFi - check credentials");
        return ESP_FAIL;
    }
    // Send response
    cJSON *response = cJSON_CreateObject();
    cJSON *success = cJSON_CreateTrue();
    cJSON_AddItemToObject(response, "success", success);
    
    const char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, response_string, strlen(response_string));
    
    free((void*)response_string);
    cJSON_Delete(response);
    cJSON_Delete(json);

    // Set the event to indicate connection success
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECT_SUCCESS_SENT);
    
    return ret;
}

// HTTP handler for WiFi scan endpoint
esp_err_t scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // Start WiFi scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true); // blocking scan
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi scan failed");
        return ESP_FAIL;
    }
    
    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        ESP_LOGW(TAG, "No WiFi networks found");
        cJSON *json = cJSON_CreateObject();
        cJSON *networks = cJSON_CreateArray();
        cJSON_AddItemToObject(json, "networks", networks);
        
        const char *json_string = cJSON_Print(json);
        httpd_resp_set_type(req, "application/json");
        esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));
        
        free((void*)json_string);
        cJSON_Delete(json);
        return send_ret;
    }
    
    // Limit to reasonable number of networks
    if (ap_count > 20) {
        ap_count = 20;
    }
    
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        free(ap_records);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get scan results");
        return ESP_FAIL;
    }

    esp_wifi_scan_stop(); // Stop scan to avoid blocking further operations
    esp_wifi_clear_ap_list(); // Clear previous AP list to avoid memory leaks
    
    // Create JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    
    for (int i = 0; i < ap_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON *ssid = cJSON_CreateString((char*)ap_records[i].ssid);
        cJSON *rssi = cJSON_CreateNumber(ap_records[i].rssi);
        cJSON *channel = cJSON_CreateNumber(ap_records[i].primary);
        
        // Determine security type
        const char *auth_mode;
        switch (ap_records[i].authmode) {
            case WIFI_AUTH_OPEN:
                auth_mode = "Open";
                break;
            case WIFI_AUTH_WEP:
                auth_mode = "WEP";
                break;
            case WIFI_AUTH_WPA_PSK:
                auth_mode = "WPA";
                break;
            case WIFI_AUTH_WPA2_PSK:
                auth_mode = "WPA2";
                break;
            case WIFI_AUTH_WPA_WPA2_PSK:
                auth_mode = "WPA/WPA2";
                break;
            case WIFI_AUTH_WPA3_PSK:
                auth_mode = "WPA3";
                break;
            default:
                auth_mode = "Unknown";
                break;
        }
        cJSON *security = cJSON_CreateString(auth_mode);
        
        cJSON_AddItemToObject(network, "ssid", ssid);
        cJSON_AddItemToObject(network, "rssi", rssi);
        cJSON_AddItemToObject(network, "channel", channel);
        cJSON_AddItemToObject(network, "security", security);
        
        cJSON_AddItemToArray(networks, network);
    }
    
    cJSON_AddItemToObject(json, "networks", networks);
    cJSON_AddNumberToObject(json, "count", ap_count);
    
    const char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    esp_err_t send_ret = httpd_resp_send(req, json_string, strlen(json_string));
    
    // Cleanup
    free(ap_records);
    free((void*)json_string);
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "WiFi scan completed, found %d networks", ap_count);
    return send_ret;
}

// Start HTTP provisioning server
esp_err_t start_provisioning_server(void)
{
    // Check if server is already running
    if (server != NULL) {
        ESP_LOGW(TAG, "HTTP provisioning server is already running");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register endpoints
        
        httpd_uri_t config_uri = {
            .uri = "/config",
            .method = HTTP_POST,
            .handler = config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_uri);

        httpd_uri_t scan_uri = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = scan_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &scan_uri);
        
        provisioning_active = true;
        ESP_LOGI(TAG, "HTTP provisioning server started on port 80");

        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

// Stop HTTP provisioning server
void stop_provisioning_server(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        provisioning_active = false;
        ESP_LOGI(TAG, "HTTP provisioning server stopped");
    }
}

void clear_wifi_provisioning(void)
{
    ESP_LOGI(TAG, "Clearing WiFi provisioning data...");
    
    /* Open NVS handle */
    nvs_handle_t nvs_handle;
    
    /* Clear WiFi credentials */
    ESP_LOGI(TAG, "Clearing WiFi credentials from NVS");
    esp_err_t ret = nvs_open("wifi_cred", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "NVS not initialized! Call nvs_flash_init() first");
        return;
    }
    if (ret == ESP_OK) {
        ESP_ERROR_CHECK(nvs_erase_all(nvs_handle));
        ESP_ERROR_CHECK(nvs_commit(nvs_handle));
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi STA data cleared");
    }
    
    ESP_LOGI(TAG, "All WiFi provisioning data cleared. Restarting...");
}

bool display_wifi_credentials(const wifi_credentials_t *credentials)
{
    if (!credentials) {
        ESP_LOGE(TAG, "Invalid credentials pointer");
        return false;
    }
    
    ESP_LOGI(TAG, "Displaying stored WiFi credentials...");
    
    /* Display credentials info */
    ESP_LOGI(TAG, "WiFi credentials found in storage");
    ESP_LOGI(TAG, "SSID: %s", credentials->ssid);
    if (strlen(credentials->password) > 0) {
        ESP_LOGI(TAG, "Password: %s", credentials->password);
    } else {
        ESP_LOGI(TAG, "Password: (none - open network)");
    }
    ESP_LOGI(TAG, "SSID length: %d bytes", strlen(credentials->ssid));
    ESP_LOGI(TAG, "Password length: %d bytes", strlen(credentials->password));
    
    return true;
}

void start_wifi_provisioning(void)
{
    /* Initialize the event group */
    wifi_event_group = xEventGroupCreate();

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register our event handler for Wi-Fi and IP events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* Check if already provisioned - load credentials once from flash */
    wifi_credentials_t saved_credentials;
    esp_err_t ret = load_wifi_credentials_from_nvs(&saved_credentials);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials found, connecting to saved network...");
        
        // Display credential info for debugging
        display_wifi_credentials(&saved_credentials);
        
        // Mark that we have credentials
        current_state.has_credentials = true;
        
        // Connect using loaded credentials
        connect_wifi_with_credentials(&saved_credentials);

        // If loaded credentials fail, provisioning mode will be started in the event handler
        
    } else {
        ESP_LOGI(TAG, "No WiFi credentials found, starting provisioning mode...");
        
        // Start in AP mode for provisioning
        start_ap_mode();
        
        // Start HTTP server for provisioning
        start_provisioning_server();
    }

    /* Wait for Wi-Fi connection */
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(wifi_event_group, WIFI_PROVISIONING_DONE_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");
}

// Connect to WiFi using provided credentials
void connect_wifi_with_credentials(wifi_credentials_t *credentials)
{
    if (!credentials) {
        ESP_LOGE(TAG, "Invalid credentials provided");
        return;
    }
    
    // Store credentials in current state for event handler use
    strncpy(current_state.ssid, credentials->ssid, sizeof(current_state.ssid) - 1);
    strncpy(current_state.password, credentials->password, sizeof(current_state.password) - 1);
    current_state.ssid[sizeof(current_state.ssid) - 1] = '\0';
    current_state.password[sizeof(current_state.password) - 1] = '\0';
    
    // Reset connection state
    current_state.connection_attempts = 0;
    current_state.connection_failed = false;
    current_state.wifi_connected = false;
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, credentials->ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (strlen(credentials->password) > 0) {
        strncpy((char*)wifi_config.sta.password, credentials->password, sizeof(wifi_config.sta.password) - 1);
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
}

// Start AP mode for provisioning
void start_ap_mode(void)
{
    // Get unique AP name based on MAC
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
    
    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .password = "123456789aSdF!_$",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK  // Use WPA2 only instead of WPA_WPA2
        },
    };
    
    // Set unique SSID with MAC address
    snprintf((char*)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "ESP32-Setup-%02X%02X", mac[4], mac[5]);
    ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));  // Use APSTA mode to enable scanning
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi APSTA started: SSID=%s, Password=%s", ap_config.ap.ssid, ap_config.ap.password);
    ESP_LOGI(TAG, "Connect to this AP and go to http://192.168.4.1 for provisioning");
}
