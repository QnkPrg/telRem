#ifndef _UDP_STREAM_H_
#define _UDP_STREAM_H_

#include "audio_element.h"
#include <sys/socket.h>
#include "audio_common.h"
#include "audio_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    audio_stream_type_t type; // Type of the audio stream
    int out_rb_size; // Size of the output ring buffer
    struct sockaddr_in dest_addr; // Destination address for UDP stream
    int task_stack; // Stack size for the task
    int buffer_len; // Length of the buffer for reading/writing
} udp_stream_cfg_t;

/**
 * @brief Initialize UDP audio stream element
 *
 * @param config Configuration structure
 * @return audio_element_handle_t Audio element handle, or NULL on error
 */
audio_element_handle_t udp_stream_init(udp_stream_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif // _UDP_STREAM_H_
