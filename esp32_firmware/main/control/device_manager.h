#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "audio_pipeline.h"
#include "esp_event.h"

#define UDP_PORT_LOCAL      12345
#define MAX_LINKED_DEVICES  5
#define DEVICE_ID_LENGTH    32

/**
 * @brief Device control commands
 */
typedef enum {
    CMD_REQUEST_TALK = 0,
    CMD_END_TALK = 1,
    CMD_GRANT_TALK = 2,
    CMD_DENY_TALK = 3,
    CMD_TALK_ENDED = 4,
    CMD_TALK_DID_NOT_END = 5,
    CMD_DOORBELL_RING = 6,
    CMD_OPEN_DOOR = 7,
    CMD_ACTIVATE_FEC = 8,
    CMD_DEACTIVATE_FEC = 9,
    CMD_FEC_ACTIVATED = 10,
    CMD_FEC_DEACTIVATED = 11
} device_command_t;

/**
 * @brief Device status
 */
typedef enum {
    STATUS_IDLE = 0,
    STATUS_AUDIO_RUNNING = 1,
    STATUS_CONNECTING = 2,
    STATUS_ERROR = 3
} device_status_t;

/**
 * @brief Linked device information
 */
typedef struct {
    char device_id[DEVICE_ID_LENGTH];
    uint32_t ip_address;
    uint16_t port;
    bool is_active;
    uint32_t last_ping;
} linked_device_t;

/**
 * @brief Device management system state
 */
typedef struct {
    linked_device_t linked_devices[MAX_LINKED_DEVICES];
    int num_linked_devices;
    int active_device_index;
    device_status_t status;
    SemaphoreHandle_t device_mutex;
    bool wifi_reset_requested;
    bool linking_mode;
    uint32_t linking_mode_timeout;
} device_control_t;

/**
 * @brief Initialize device control system
 * 
 * @return ESP_OK on success
 */
esp_err_t device_control_init(void);

/**
 * @brief Main device manager task
 * 
 * This task creates a TCP server that listens for control commands
 * and manages multi-client connections with audio streaming.
 * Audio pipelines are initialized internally for each client.
 * 
 * @param arg Task arguments (unused)
 */
void device_manager_task(void *arg);

/**
 * @brief Button monitoring task
 * 
 * Monitors button press for WiFi reset functionality
 * 
 * @param arg Task arguments (unused)
 */
void button_monitor_task(void *arg);

/**
 * @brief Get current device status
 * 
 * @return Current device status
 */
device_status_t get_device_status(void);

/**
 * @brief Initialize peripheral system
 * 
 * @return ESP_OK on success
 */
esp_err_t init_peripheral_system(void);

/**
 * @brief Broadcast doorbell ring to all connected clients
 */
void broadcast_doorbell_ring(void);

/**
 * @brief Request talk permission for a client
 * 
 * @param client_index Index of the client requesting permission
 * @return true if permission granted, false otherwise
 */
bool request_talk_permission(int client_index);

/**
 * @brief Release talk permission for a client
 * 
 * @param client_index Index of the client releasing permission
 */
bool release_talk_permission(int client_index);

/**
 * @brief Start audio pipelines for a specific client
 * 
 * @param client_index Index of the client
 * @return ESP_OK on success
 */
esp_err_t start_audio_pipelines_for_client(int client_index);

/**
 * @brief Clean up a client connection
 * 
 * @param client_index Index of the client to clean up
 */
void cleanup_client(int client_index);

/**
 * @brief Handle client commands
 * 
 * @param client_index Index of the client
 * @param command Command received from client
 */
void handle_client_command(int client_index, int command);

/**
 * @brief Client handler task
 * 
 * @param param Pointer to client index
 */
void client_handler_task(void *param);

/**
 * @brief Add a new client to the system
 * 
 * @param client_sock Client socket
 * @param client_ip Client IP address
 * @return true if client added successfully, false otherwise
 */
bool add_new_client(int client_sock, uint32_t client_ip);

/**
 * @brief Broadcast doorbell ring to all connected clients
 */
void broadcast_doorbell_ring(void);

#endif // DEVICE_MANAGER_H
