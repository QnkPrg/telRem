#include "video_manager.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "esp_heap_caps.h"

static const char *TAG = "VIDEO_MANAGER";

// Video packet types
#define VIDEO_PACKAGE 1

// Header field offsets
#define VIDEO_HEADER_TYPE_OFFSET      0   // Package type (1 byte)
#define VIDEO_HEADER_FRAME_ID_OFFSET  1   // Frame ID (4 bytes)
#define VIDEO_HEADER_TIMESTAMP_OFFSET 5   // Timestamp (8 bytes)
#define VIDEO_HEADER_LENGTH_OFFSET    13  // Packet length (2 bytes)
#define VIDEO_HEADER_PACKET_SEQ_OFFSET 15 // Packet sequence (2 bytes)
#define VIDEO_HEADER_TOTAL_PACKETS_OFFSET 17 // Total packets (2 bytes)
#define VIDEO_HEADER_DATA_OFFSET      19  // Start of data payload

// Header field sizes
#define VIDEO_HEADER_TYPE_SIZE        1
#define VIDEO_HEADER_FRAME_ID_SIZE    4
#define VIDEO_HEADER_TIMESTAMP_SIZE   8
#define VIDEO_HEADER_LENGTH_SIZE      2
#define VIDEO_HEADER_PACKET_SEQ_SIZE  2
#define VIDEO_HEADER_TOTAL_PACKETS_SIZE 2

// Video manager info structure
typedef struct {
    bool is_streaming;
    bool stop_requested;
    in_addr_t remote_addr;
    int udp_socket;
    struct sockaddr_in dest_addr;
    uint32_t frame_id;         // Frame identifier (increments per frame)
} video_manager_info_t;


// Video streaming configuration
#define VIDEO_FPS 15  // Frames per second
#define VIDEO_FRAME_INTERVAL_MS ((1000 + VIDEO_FPS/2) / VIDEO_FPS)
// Each frame sent is fragmented into 5/6 packets, and in between frames there is a delay
// this define is used to compensate for the time taken in sending the packets.
#define DELAY_COMPENSATION_MS 50

// Global video manager state
static video_manager_info_t video_info = {0};
static TaskHandle_t video_task_handle = NULL;
static SemaphoreHandle_t video_info_mutex = NULL;

// ESP32 Korvo 2 v3 camera pin configuration
#define CAM_PIN_PWDN    -1  // Power down pin
#define CAM_PIN_RESET   -1  // Software reset will be performed
#define CAM_PIN_XCLK    40  // Clock pin
#define CAM_PIN_SIOD    17  // SDA pin for camera I2C
#define CAM_PIN_SIOC    18  // SCL pin for camera I2C
#define CAM_PIN_VSYNC   21  // Vertical sync
#define CAM_PIN_HREF    38  // Horizontal reference
#define CAM_PIN_PCLK    11  // Pixel clock
#define CAM_PIN_D7      39  // Data pin 7
#define CAM_PIN_D6      41  // Data pin 6
#define CAM_PIN_D5      42  // Data pin 5
#define CAM_PIN_D4      12  // Data pin 4
#define CAM_PIN_D3      3   // Data pin 3
#define CAM_PIN_D2      14  // Data pin 2
#define CAM_PIN_D1      47  // Data pin 1
#define CAM_PIN_D0      13  // Data pin 0

const BaseType_t CORE_PIN = 0;

// Forward declaration
/**
 * @brief Capture and send a single frame
 * @return ESP_OK on success, ESP_FAIL on error
 */
static esp_err_t _video_manager_send_frame(void);


esp_err_t video_manager_init(void)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Initializing ESP32-CAM...");
    
    // Camera configuration
    camera_config_t config = {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = 20000000,  // 20MHz
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,  // JPEG format for compression
        .frame_size = VIDEO_QUALITY,     // VGA resolution
        .jpeg_quality = JPEG_QUALITY,    // JPEG quality
        .fb_count = 2,                   // Double buffering to reduce contention
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY
    };

    // Initialize the camera
    ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize video manager state
    memset(&video_info, 0, sizeof(video_info));
    video_info.udp_socket = -1;
    video_info.is_streaming = false;
    video_info.frame_id = 0;
    video_info.stop_requested = false;

    // Create mutex for video_info protection
    video_info_mutex = xSemaphoreCreateMutex();
    if (video_info_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create video_info mutex");
        esp_camera_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ESP32-CAM initialized successfully");
    return ESP_OK;
}

// Video streaming task
static void _video_streaming_task(void *param)
{
    ESP_LOGI(TAG, "Video streaming task started");
    
    bool should_continue = true;

    while (should_continue) {
        // Check if we should continue streaming (thread-safe)
        xSemaphoreTake(video_info_mutex, portMAX_DELAY);
            should_continue = !video_info.stop_requested;
        xSemaphoreGive(video_info_mutex);
        
        if (should_continue) {
            esp_err_t ret = _video_manager_send_frame();
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "Failed to send video frame");
            }

            vTaskDelay(pdMS_TO_TICKS(VIDEO_FRAME_INTERVAL_MS - DELAY_COMPENSATION_MS));
        }
    }

    xSemaphoreTake(video_info_mutex, portMAX_DELAY);
        video_info.is_streaming = false;
        if (video_info.udp_socket >= 0) {
            close(video_info.udp_socket);
            video_info.udp_socket = -1;
        }
    xSemaphoreGive(video_info_mutex);
    
    ESP_LOGI(TAG, "Video streaming task ended");
    video_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t video_manager_start_streaming(in_addr_t client_ip)
{
    xSemaphoreTake(video_info_mutex, portMAX_DELAY);
    
        if (video_info.is_streaming) {
            ESP_LOGW(TAG, "Video streaming already active");
            xSemaphoreGive(video_info_mutex);
            return ESP_OK;
        }

        // Create UDP socket for video streaming
        video_info.udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (video_info.udp_socket < 0) {
            ESP_LOGE(TAG, "Failed to create UDP socket for video");
            xSemaphoreGive(video_info_mutex);
            return ESP_FAIL;
        }

        // Configure destination address
        video_info.dest_addr.sin_family = AF_INET;
        video_info.dest_addr.sin_addr.s_addr = client_ip;
        video_info.dest_addr.sin_port = htons(VIDEO_UDP_PORT);
        video_info.remote_addr = client_ip;

        video_info.is_streaming = true;
        video_info.stop_requested = false;
    
    xSemaphoreGive(video_info_mutex);

    // Convert IP for logging
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr addr = { .s_addr = client_ip };
    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);

    // Create video streaming task
    BaseType_t task_created = xTaskCreate(
        _video_streaming_task,
        "video_stream",
        16384,  // Stack size
        NULL,  // Parameters
        4,     // Priority+
        &video_task_handle
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create video streaming task");
        xSemaphoreTake(video_info_mutex, portMAX_DELAY);
            video_info.is_streaming = false;
            close(video_info.udp_socket);
            video_info.udp_socket = -1;
        xSemaphoreGive(video_info_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started video streaming to %s:%d", ip_str, VIDEO_UDP_PORT);
    return ESP_OK;
}

esp_err_t video_manager_stop_streaming(void)
{
    xSemaphoreTake(video_info_mutex, portMAX_DELAY);
        if (!video_info.is_streaming) {
            ESP_LOGW(TAG, "Video streaming not active");
            xSemaphoreGive(video_info_mutex);
            return ESP_OK;
        }

        video_info.stop_requested = true;

    xSemaphoreGive(video_info_mutex);

    // Wait for the streaming task to finish
    bool thread_running = true;
    while (thread_running) {
        xSemaphoreTake(video_info_mutex, portMAX_DELAY);
            thread_running = video_info.is_streaming;
        xSemaphoreGive(video_info_mutex);
    }

    return ESP_OK;
}

static esp_err_t _video_manager_send_frame(void)
{
    // Check streaming state (thread-safe)
    xSemaphoreTake(video_info_mutex, portMAX_DELAY);
    
        if (!video_info.is_streaming) {
            xSemaphoreGive(video_info_mutex);
            return ESP_ERR_INVALID_STATE;
        }
        
        // Get next frame ID and increment (thread-safe)
        uint32_t current_frame_id = video_info.frame_id++;
        
        // Copy socket info for use outside mutex
        int udp_socket = video_info.udp_socket;
        struct sockaddr_in dest_addr = video_info.dest_addr;
    
    xSemaphoreGive(video_info_mutex);

    
    // Capture frame from camera
    camera_fb_t * fb = esp_camera_fb_get();

    // Calculate number of packets needed
    uint32_t total_packets = (fb->len + MAX_VIDEO_DATA_SIZE - 1) / MAX_VIDEO_DATA_SIZE;
    struct timeval timestamp;
    gettimeofday(&timestamp, NULL); // Get current timestamp
    int64_t time_ms = (int64_t)timestamp.tv_sec * 1000L + (int64_t)timestamp.tv_usec / 1000L;
    uint16_t total_packets_16 = (uint16_t)total_packets; // Convert to 16-bit

    // Send frame in packets using zero-copy transmission
    for (uint32_t packet_seq = 0; packet_seq < total_packets; packet_seq++) {
        uint32_t offset = packet_seq * MAX_VIDEO_DATA_SIZE;
        uint32_t packet_data_size = (offset + MAX_VIDEO_DATA_SIZE > fb->len) ? 
                                   (fb->len - offset) : MAX_VIDEO_DATA_SIZE;

        // Build header in separate buffer (no large memcpy needed)
        uint8_t header[VIDEO_STREAM_HEADER_LEN];
        uint16_t packet_length = packet_data_size;
        uint16_t packet_seq_16 = (uint16_t)packet_seq;
        
        // Build video packet header efficiently
        header[VIDEO_HEADER_TYPE_OFFSET] = VIDEO_PACKAGE;
        memcpy(&header[VIDEO_HEADER_FRAME_ID_OFFSET], &current_frame_id, VIDEO_HEADER_FRAME_ID_SIZE);
        memcpy(&header[VIDEO_HEADER_TIMESTAMP_OFFSET], &time_ms, VIDEO_HEADER_TIMESTAMP_SIZE);
        memcpy(&header[VIDEO_HEADER_LENGTH_OFFSET], &packet_length, VIDEO_HEADER_LENGTH_SIZE);
        memcpy(&header[VIDEO_HEADER_PACKET_SEQ_OFFSET], &packet_seq_16, VIDEO_HEADER_PACKET_SEQ_SIZE);
        memcpy(&header[VIDEO_HEADER_TOTAL_PACKETS_OFFSET], &total_packets_16, VIDEO_HEADER_TOTAL_PACKETS_SIZE);

        // Use scatter-gather I/O to avoid copying video data
        struct iovec iov[2];
        iov[0].iov_base = header;
        iov[0].iov_len = VIDEO_STREAM_HEADER_LEN;
        iov[1].iov_base = fb->buf + offset;  // Direct from camera buffer - zero copy!
        iov[1].iov_len = packet_data_size;

        struct msghdr msg = {
            .msg_name = &dest_addr,
            .msg_namelen = sizeof(dest_addr),
            .msg_iov = iov,
            .msg_iovlen = 2,
            .msg_control = NULL,
            .msg_controllen = 0,
            .msg_flags = 0
        };

        // Zero-copy transmission
        int sent = sendmsg(udp_socket, &msg, 0);

        
        if (sent < 0) {
            if (errno == ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(50)); // Back off briefly on memory error
            }
            ESP_LOGD(TAG, "Failed to send video packet %" PRIu32 "/%" PRIu32 " (frame %" PRIu32 ") errno: %s",
                      packet_seq + 1, total_packets, current_frame_id, strerror(errno));
            esp_camera_fb_return(fb);
            return ESP_FAIL;
        }
        // Yield to allow network tasks to handle the packages,
        // to minimize ENOMEM errors when sending
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
}

void video_manager_cleanup(void)
{
    video_manager_stop_streaming();
    
    // Deinitialize camera
    esp_camera_deinit();
    
    // Clean up mutex
    if (video_info_mutex != NULL) {
        vSemaphoreDelete(video_info_mutex);
        video_info_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Video manager cleaned up");
}
