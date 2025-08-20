#ifndef AUDIO_PIPELINE_MANAGER_H
#define AUDIO_PIPELINE_MANAGER_H

#include "audio_pipeline.h"
#include "audio_element.h"
#include <netinet/in.h>

struct audio_pipeline_manager_info {
    audio_pipeline_handle_t pipeline_send;
    audio_pipeline_handle_t pipeline_recv;
    audio_element_handle_t i2s_reader;
    audio_element_handle_t udp_writer;
    audio_element_handle_t udp_reader;
    audio_element_handle_t i2s_writer;
    in_addr_t remote_addr;
};

/**
 * @brief Initialize audio pipelines for send and receive
 * 
 * @param audio_pipelines_info Pointer to audio pipeline manager info structure
 * @return ESP_OK on success
 */
esp_err_t audio_pipelines_init(struct audio_pipeline_manager_info *audio_pipelines_info);

/**
 * @brief Cleanup audio pipeline resources
 * 
 * @param audio_pipelines_info Pointer to audio pipeline manager info structure
 */
void audio_pipeline_cleanup(struct audio_pipeline_manager_info *audio_pipelines_info);

#endif // AUDIO_PIPELINE_MANAGER_H
