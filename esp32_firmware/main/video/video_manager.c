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

static const char *TAG = "VIDEO_MANAGER";

// Video streaming configuration
#define VIDEO_FPS 20  // Frames per second
#define VIDEO_FRAME_INTERVAL_MS (1000 / VIDEO_FPS)

// Global video manager state
static video_manager_info_t video_info = {0};
static TaskHandle_t video_task_handle = NULL;

/*
 * Video packet format (enhanced for precise frame completion):
 * - 1 byte: package type (VIDEO_PACKAGE = 2)
 * - 4 bytes: frame ID (identifies which frame this packet belongs to)
 * - 8 bytes: timestamp (ms since EPOCH)
 * - 2 bytes: packet length (data size)
 * - 2 bytes: packet sequence (position of this packet within the frame)
 * - 2 bytes: total packets (total number of packets in complete frame)
 * - N bytes: video data (JPEG frame fragment)
 * 
 * Total header: 15 bytes
 * Max packet size: 1400 bytes (MTU-safe)
 * Max data per packet: 1385 bytes (1400 - 15)
 */

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
        .fb_count = 2,                   // Double buffering
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY
    };

    // Initialize the camera
    ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get camera sensor handle
    sensor_t * s = esp_camera_sensor_get();
    if (s != NULL) {
        // Configure sensor settings for better video quality
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 0);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 to 6 (0-No Effect, 1-Negative, 2-Grayscale, 3-Red Tint, 4-Green Tint, 5-Blue Tint, 6-Sepia)
        s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
        s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
        s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
        s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
        s->set_aec2(s, 0);           // 0 = disable , 1 = enable
        s->set_ae_level(s, 0);       // -2 to 2
        s->set_aec_value(s, 300);    // 0 to 1200
        s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
        s->set_agc_gain(s, 0);       // 0 to 30
        s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
        s->set_bpc(s, 0);            // 0 = disable , 1 = enable
        s->set_wpc(s, 1);            // 0 = disable , 1 = enable
        s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
        s->set_lenc(s, 1);           // 0 = disable , 1 = enable
        s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
        s->set_vflip(s, 0);          // 0 = disable , 1 = enable
        s->set_dcw(s, 1);            // 0 = disable , 1 = enable
        s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
    }

    // Initialize video manager state
    memset(&video_info, 0, sizeof(video_info));
    video_info.udp_socket = -1;
    video_info.is_streaming = false;
    video_info.frame_id = 0;

    ESP_LOGI(TAG, "ESP32-CAM initialized successfully");
    return ESP_OK;
}

// Video streaming task
static void video_streaming_task(void *param)
{
    ESP_LOGI(TAG, "Video streaming task started");
    
    while (video_info.is_streaming) {
        esp_err_t ret = video_manager_send_frame();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send video frame");
            // Continue trying to send frames
        }
        
        vTaskDelay(pdMS_TO_TICKS(VIDEO_FRAME_INTERVAL_MS));
    }
    
    ESP_LOGI(TAG, "Video streaming task ended");
    video_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t video_manager_start_streaming(in_addr_t client_ip)
{
    if (video_info.is_streaming) {
        ESP_LOGW(TAG, "Video streaming already active");
        return ESP_OK;
    }

    // Create UDP socket for video streaming
    video_info.udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (video_info.udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket for video");
        return ESP_FAIL;
    }

    // Configure destination address
    video_info.dest_addr.sin_family = AF_INET;
    video_info.dest_addr.sin_addr.s_addr = client_ip;
    video_info.dest_addr.sin_port = htons(VIDEO_UDP_PORT);
    video_info.remote_addr = client_ip;

    video_info.is_streaming = true;

    // Convert IP for logging
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr addr = { .s_addr = client_ip };
    inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);

    // Create video streaming task
    BaseType_t task_created = xTaskCreate(
        video_streaming_task,
        "video_stream",
        4096,  // Stack size
        NULL,  // Parameters
        4,     // Priority
        &video_task_handle
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create video streaming task");
        video_manager_stop_streaming();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started video streaming to %s:%d", ip_str, VIDEO_UDP_PORT);
    return ESP_OK;
}

esp_err_t video_manager_stop_streaming(void)
{
    if (!video_info.is_streaming) {
        ESP_LOGW(TAG, "Video streaming not active");
        return ESP_OK;
    }

    video_info.is_streaming = false;

    // Wait for streaming task to finish
    if (video_task_handle != NULL) {
        // Give the task time to finish naturally
        for (int i = 0; i < 10 && video_task_handle != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // Force delete if still running
        if (video_task_handle != NULL) {
            vTaskDelete(video_task_handle);
            video_task_handle = NULL;
        }
    }

    if (video_info.udp_socket >= 0) {
        close(video_info.udp_socket);
        video_info.udp_socket = -1;
    }

    ESP_LOGI(TAG, "Video streaming stopped");
    return ESP_OK;
}

esp_err_t video_manager_send_frame(void)
{
    if (!video_info.is_streaming) {
        return ESP_ERR_INVALID_STATE;
    }

    // Capture frame from camera
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return ESP_FAIL;
    }

    // Calculate number of packets needed
    uint32_t total_packets = (fb->len + MAX_VIDEO_DATA_SIZE - 1) / MAX_VIDEO_DATA_SIZE;
    uint32_t current_frame_id = video_info.frame_id++;
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
            .msg_name = &video_info.dest_addr,
            .msg_namelen = sizeof(video_info.dest_addr),
            .msg_iov = iov,
            .msg_iovlen = 2,
            .msg_control = NULL,
            .msg_controllen = 0,
            .msg_flags = 0
        };

        // Zero-copy transmission
        int sent = sendmsg(video_info.udp_socket, &msg, 0);
        
        if (sent < 0) {
            ESP_LOGE(TAG, "Failed to send video packet %" PRIu32 "/%" PRIu32 " (frame %" PRIu32 ") errno: %s",
                      packet_seq + 1, total_packets, current_frame_id, strerror(errno));
            esp_camera_fb_return(fb);
            return ESP_FAIL;
        }
    }

    // Return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
}

bool video_manager_is_streaming(void)
{
    return video_info.is_streaming;
}

void video_manager_cleanup(void)
{
    video_manager_stop_streaming();
    
    // Deinitialize camera
    esp_camera_deinit();
    
    ESP_LOGI(TAG, "Video manager cleaned up");
}
