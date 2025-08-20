#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include "esp_err.h"
#include "esp_camera.h"
#include "lwip/sockets.h"

#ifdef __cplusplus
extern "C" {
#endif

// Video streaming configuration
#define VIDEO_UDP_PORT 12346
#define MAX_FRAME_SIZE 32768  
#define VIDEO_QUALITY FRAMESIZE_VGA  // 640x480
#define JPEG_QUALITY 20  // JPEG quality (0-63, lower is higher quality)

// Video packet types
#define VIDEO_PACKAGE 1

// Video packet header structure (modified from UDP stream)
// 1 Byte for package type
// 4 Bytes for frame ID (instead of sequence number)
// 8 Bytes for timestamp (for audio sync)
// 2 Bytes for packet length
// 2 Bytes for packet sequence within frame
// 2 Bytes for total packets in frame
// Total header length is 19 Bytes (1 + 4 + 8 + 2 + 2 + 2)
#define VIDEO_STREAM_HEADER_LEN 19

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

#define MAX_VIDEO_PACKET_SIZE 1400  // MTU-safe packet size
#define MAX_VIDEO_DATA_SIZE (MAX_VIDEO_PACKET_SIZE - VIDEO_STREAM_HEADER_LEN)

// Remove old helper macros as we now have explicit fields
// Frame ID is in its own field, packet sequence is in its own field

// Video manager info structure
typedef struct {
    bool is_streaming;
    in_addr_t remote_addr;
    int udp_socket;
    struct sockaddr_in dest_addr;
    uint32_t frame_id;         // Frame identifier (increments per frame)
} video_manager_info_t;

/**
 * @brief Initialize ESP32-CAM
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t video_manager_init(void);

/**
 * @brief Start video streaming task to client
 * @param client_ip IP address of client to stream to
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t video_manager_start_streaming(in_addr_t client_ip);

/**
 * @brief Stop video streaming and task
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t video_manager_stop_streaming(void);

/**
 * @brief Capture and send a single frame
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t video_manager_send_frame(void);

/**
 * @brief Get video streaming status
 * @return true if streaming, false otherwise
 */
bool video_manager_is_streaming(void);

/**
 * @brief Cleanup video manager resources
 */
void video_manager_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_MANAGER_H
