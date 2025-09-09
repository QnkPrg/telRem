#ifndef VIDEO_MANAGER_H
#define VIDEO_MANAGER_H

#include "esp_err.h"
#include "esp_camera.h"
#include "lwip/sockets.h"

#ifdef __cplusplus
extern "C" {
#endif

// Video packet header structure (modified from UDP stream)
// 1 Byte for package type
// 4 Bytes for frame ID (instead of sequence number)
// 8 Bytes for timestamp (for audio sync)
// 2 Bytes for packet length
// 2 Bytes for packet sequence within frame
// 2 Bytes for total packets in frame
// Total header length is 19 Bytes (1 + 4 + 8 + 2 + 2 + 2)
#define VIDEO_STREAM_HEADER_LEN 19

// Video streaming configuration
#define VIDEO_UDP_PORT 12346
#define MAX_FRAME_SIZE 32768  
#define VIDEO_QUALITY FRAMESIZE_VGA  // 640x480
#define JPEG_QUALITY 40  // JPEG quality (0-63, lower is higher quality)

#define MAX_VIDEO_PACKET_SIZE 1400  // MTU-safe packet size
#define MAX_VIDEO_DATA_SIZE (MAX_VIDEO_PACKET_SIZE - VIDEO_STREAM_HEADER_LEN)


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
 * @brief Cleanup video manager resources
 */
void video_manager_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_MANAGER_H
