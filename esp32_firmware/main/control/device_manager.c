#include "device_manager.h"
#include "../audio/audio_pipeline_manager.h"
#include "../video/video_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "DEVICE_MANAGER";

// Forward declarations for static functions

/**
 * @brief Request talk permission for a client
 * @param client_index Index of the client requesting permission
 */
static bool _request_talk_permission(int client_index);

/**
 * @brief Release talk permission for a client
 * @param client_index Index of the client releasing permission
 */
static bool _release_talk_permission(int client_index);

/**
 * @brief Stop audio and video streams
 */
static void _stop_audio_and_video(void);

/**
 * @brief Start audio and video streams for a client
 * @param client_index Index of the client to start streams for
 */
static esp_err_t _start_audio_and_video_for_client(int client_index);

/**
 * @brief Cleanup resources for a client
 * @param client_index Index of the client to clean up
 */
static void _cleanup_client(int client_index);

/**
 * @brief Handle a command from a client
 * @param client_index Index of the client sending the command
 * @param command Command to handle
 */
static void _handle_client_command(int client_index, int command);

/**
 * @brief Task handler for individual clients
 * @param param Pointer to client-specific parameters
 */
static void _client_handler_task(void *param);

/**
 * @brief Add a new client to the manager
 * @param client_sock Socket descriptor for the new client
 * @param client_ip IP address of the new client
 */
static bool _add_new_client(int client_sock, in_addr_t client_ip);

/**
 * @brief Main device manager task
 * @param arg Pointer to task-specific arguments
 */
static void _device_manager_task(void *arg);

#define MAX_CLIENTS 5

typedef struct {
    int socket;
    in_addr_t ip_address;
    bool is_connected;
    TaskHandle_t task_handle;
} tcp_client_t;

// Client management
static tcp_client_t clients[MAX_CLIENTS];
static SemaphoreHandle_t clients_mutex = NULL;
static int active_talker_index = -1;  // -1 means no one talking
static SemaphoreHandle_t talker_mutex = NULL;

// Global audio pipeline state - shared by all clients, only one can use at a time
#define INACTIVE_CLIENT_INDEX -1
typedef struct {
    struct audio_pipeline_manager_info audio_pipelines_info;
    int active_client_index;
} global_audio_state_t;

static global_audio_state_t audio_info;

esp_err_t device_manager_init(void)
{
    if(clients_mutex != NULL || talker_mutex != NULL) {
        ESP_LOGW(TAG, "Device manager already initialized");
        return ESP_FAIL;
    }

    // Initialize mutexes
    clients_mutex = xSemaphoreCreateMutex();
    talker_mutex = xSemaphoreCreateMutex();
    
    if (clients_mutex == NULL || talker_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_FAIL;
    }
    
    // Initialize clients array
    memset(clients, 0, sizeof(clients));

    // Initialize global audio pipeline state
    memset(&audio_info, 0, sizeof(audio_info));
    audio_info.active_client_index = INACTIVE_CLIENT_INDEX;

    xTaskCreate(_device_manager_task, "device_manager", 4096, NULL, 5, NULL);

    return ESP_OK;
}

// Broadcast doorbell ring to all connected clients
void broadcast_doorbell_ring(void) {
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
        uint32_t doorbell_cmd = CMD_DOORBELL_RING;
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].is_connected) {
                send(clients[i].socket, &doorbell_cmd, sizeof(doorbell_cmd), MSG_DONTWAIT);
                ESP_LOGI(TAG, "Sent doorbell ring to client %d", i);
            }
        }
    xSemaphoreGive(clients_mutex);
}

// Request talk permission for a client
static bool _request_talk_permission(int client_index) {
    xSemaphoreTake(talker_mutex, portMAX_DELAY);
        if (active_talker_index == INACTIVE_CLIENT_INDEX) {
            active_talker_index = client_index;
            xSemaphoreGive(talker_mutex);
            ESP_LOGI(TAG, "Talk permission granted to client %d", client_index);
            return true;
        }
        
        ESP_LOGW(TAG, "Talk permission denied to client %d", client_index);
        ESP_LOGI(TAG, "Another client is currently talking %d", active_talker_index);
    xSemaphoreGive(talker_mutex);
    return false;
}

// Release talk permission for a client
static bool _release_talk_permission(int client_index) {
    bool released = false;
    xSemaphoreTake(talker_mutex, portMAX_DELAY);
        if (active_talker_index == client_index) {
            active_talker_index = INACTIVE_CLIENT_INDEX;
            
            // Reset audio state
            audio_info.active_client_index = INACTIVE_CLIENT_INDEX;
            ESP_LOGI(TAG, "Audio pipelines stopped and cleaned up for client %d", client_index);
            
            released = true;
        }
    xSemaphoreGive(talker_mutex);
    return released;
}

static void _stop_audio_and_video(void) {
            
    // Stop and cleanup audio pipelines
    ESP_LOGI(TAG, "Stopping audio pipelines");
    audio_pipeline_cleanup(&audio_info.audio_pipelines_info);
    
    // Stop video streaming
    video_manager_stop_streaming();
    ESP_LOGI(TAG, "Video streaming stopped");

}

// Start audio and video for a specific client
static esp_err_t _start_audio_and_video_for_client(int client_index) {

    // Get client IP address
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
        in_addr_t client_ip = clients[client_index].ip_address;
    xSemaphoreGive(clients_mutex);
    audio_info.audio_pipelines_info.remote_addr = client_ip;
    
    // Convert to string for logging
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr addr = { .s_addr = client_ip };
    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
    
    ESP_LOGI(TAG, "Initializing audio pipelines for client %d (IP: %s)", client_index, ip_str);
    
    // Initialize both pipelines with client IP (no conversion needed)
    esp_err_t ret = audio_pipelines_init(&audio_info.audio_pipelines_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio pipelines for client %d", client_index);
        return ESP_FAIL;
    }
    
    // Start both pipelines
    esp_err_t ret_send = audio_pipeline_run(audio_info.audio_pipelines_info.pipeline_send);
    esp_err_t ret_recv = audio_pipeline_run(audio_info.audio_pipelines_info.pipeline_recv);

    if (ret_send != ESP_OK || ret_recv != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio pipelines for client %d", client_index);
        audio_pipeline_cleanup(&audio_info.audio_pipelines_info);
        return ESP_FAIL;
    }
    
    // Start video streaming automatically with audio
    esp_err_t video_ret = video_manager_start_streaming(client_ip);
    if (video_ret == ESP_OK) {
        ESP_LOGI(TAG, "Video streaming started for client %d", client_index);
    } else {
        ESP_LOGW(TAG, "Failed to start video streaming for client %d", client_index);
        audio_pipeline_cleanup(&audio_info.audio_pipelines_info);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio pipelines started successfully for client %d (IP: %s)", client_index, ip_str);
    return ESP_OK;
}

// Clean up a client connection
static void _cleanup_client(int client_index) {

    xSemaphoreTake(talker_mutex, portMAX_DELAY);
        if (active_talker_index == client_index) {
            _stop_audio_and_video();
        }
    xSemaphoreGive(talker_mutex);
    
    _release_talk_permission(client_index);

    xSemaphoreTake(clients_mutex, portMAX_DELAY);        
        close(clients[client_index].socket);
        clients[client_index].is_connected = false;
        clients[client_index].task_handle = NULL;
        clients[client_index].socket = 0;
        clients[client_index].ip_address = 0;
        
        ESP_LOGI(TAG, "Client %d cleaned up", client_index);
    xSemaphoreGive(clients_mutex);
}

// Handle client commands
static void _handle_client_command(int client_index, int command) {
    switch (command) {
        case CMD_REQUEST_TALK:
            if (_request_talk_permission(client_index)) {
                // Grant permission and start audio with this client's IP
                uint32_t response = CMD_GRANT_TALK;
                send(clients[client_index].socket, &response, sizeof(response), 0);
                _start_audio_and_video_for_client(client_index);
            } else {
                uint32_t response = CMD_DENY_TALK;
                send(clients[client_index].socket, &response, sizeof(response), 0);
            }
            break;
            
        case CMD_END_TALK:

            if(_release_talk_permission(client_index)) {
                uint32_t response = CMD_TALK_ENDED;
                send(clients[client_index].socket, &response, sizeof(response), 0);
                _stop_audio_and_video();
            }
            else {
                ESP_LOGW(TAG, "Failed to release talk permission for client %d", client_index);
                uint32_t response = CMD_TALK_DID_NOT_END;
                send(clients[client_index].socket, &response, sizeof(response), 0);
            }
            break;
            
        case CMD_OPEN_DOOR:
            ESP_LOGI(TAG, "Door open command received from client %d", client_index);
            // Send confirmation back to client
            {
                uint32_t response = CMD_OPEN_DOOR;
                send(clients[client_index].socket, &response, sizeof(response), 0);
            }
            ESP_LOGI(TAG, "Door opened by client %d, UART message sent", client_index);
            break;
        default:
            ESP_LOGW(TAG, "Unknown command %d from client %d", command, client_index);
            break;
    }
}

// Client handler task
static void _client_handler_task(void *param) {
    int client_index = *(int*)param;
    int sock = clients[client_index].socket;
    
    ESP_LOGI(TAG, "Client handler task started for client %d", client_index);
    
    while (clients[client_index].is_connected) {
        int command = 0;
        int recv_result = recv(sock, &command, sizeof(command), 0);
        
        if (recv_result > 0) {
            ESP_LOGI(TAG, "Client %d received command: %d", client_index, command);
            _handle_client_command(client_index, command);
        } else if (recv_result == 0) {
            // Client disconnected
            ESP_LOGI(TAG, "Client %d disconnected", client_index);
            _cleanup_client(client_index);
            break;
        } else {
            
            ESP_LOGE(TAG, "Client %d receive error: %s", client_index, strerror(errno));
            _cleanup_client(client_index);
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent busy waiting
    }
    
    ESP_LOGI(TAG, "Client handler task ending for client %d", client_index);
    vTaskDelete(NULL);
}

// Add a new client to the system
static bool _add_new_client(int client_sock, in_addr_t client_ip) {
    xSemaphoreTake(clients_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].is_connected) {
                clients[i].socket = client_sock;
                clients[i].ip_address = client_ip;
                clients[i].is_connected = true;
                
                // Create dedicated task for this client
                char task_name[32];
                snprintf(task_name, sizeof(task_name), "client_%d", i);
                
                // Pass the client index to the task
                static int client_indices[MAX_CLIENTS];
                client_indices[i] = i;
                
                BaseType_t result = xTaskCreate(_client_handler_task, task_name, 4096, 
                                            &client_indices[i], 5, &clients[i].task_handle);
                
                if (result == pdPASS) {
                    // Convert IP for logging
                    char ip_str[INET_ADDRSTRLEN];
                    struct in_addr addr = { .s_addr = client_ip };
                    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
                    
                    ESP_LOGI(TAG, "Added new client %d from IP %s", i, ip_str);
                    xSemaphoreGive(clients_mutex);
                    return true;
                } else {
                    ESP_LOGE(TAG, "Failed to create task for client %d", i);
                    clients[i].is_connected = false;
                    xSemaphoreGive(clients_mutex);
                    return false;
                }
            }
        }
    xSemaphoreGive(clients_mutex);
    
    ESP_LOGW(TAG, "No available client slots");
    return false;
}

static void _device_manager_task(void *arg)
{
    int tcp_sock = -1;
    
    // Create a TCP socket to listen for control commands
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(UDP_PORT_LOCAL),
    };
    
    tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_sock < 0) {
        ESP_LOGE(TAG, "Failed to create TCP socket");
        vTaskDelete(NULL);
    }
    
    // Set socket options to allow reuse
    int opt = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(tcp_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind TCP socket");
        close(tcp_sock);
        vTaskDelete(NULL);
    }
    
    // Listen for up to MAX_CLIENTS connections
    if (listen(tcp_sock, MAX_CLIENTS) < 0) {
        ESP_LOGE(TAG, "Failed to listen on TCP socket");
        close(tcp_sock);
        vTaskDelete(NULL);
    }
    
    ESP_LOGI(TAG, "Multi-client audio control server listening on port %d", UDP_PORT_LOCAL);
    
    while (1) {
        ESP_LOGI(TAG, "Waiting for TCP connection...");
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(tcp_sock, (struct sockaddr *)&client_addr, &addr_len);
        
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Failed to accept TCP connection");
            continue;
        }
        
        uint32_t client_ip = ntohl(client_addr.sin_addr.s_addr);
        ESP_LOGI(TAG, "TCP connection from %d.%d.%d.%d", 
                 (int)((client_ip >> 24) & 0xFF), (int)((client_ip >> 16) & 0xFF), 
                 (int)((client_ip >> 8) & 0xFF), (int)(client_ip & 0xFF));

        // Try to add the new client
        if (!_add_new_client(client_sock, client_addr.sin_addr.s_addr)) {
            // Max clients reached or error, reject connection
            ESP_LOGW(TAG, "Rejecting connection - max clients reached or error");
            close(client_sock);
        }
    }
}
