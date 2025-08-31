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
    CMD_OPEN_DOOR = 7
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
