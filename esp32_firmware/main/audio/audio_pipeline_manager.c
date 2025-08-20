#include "audio_pipeline_manager.h"
#include <string.h>
#include "esp_log.h"
#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "udp_stream.h"
#include "board.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define I2S_SAMPLE_RATE     8000
#define UDP_PORT_LOCAL      12345

static const char *TAG = "AUDIO_MANAGER";

esp_err_t audio_pipelines_init(struct audio_pipeline_manager_info *audio_pipelines_info)
{
    if (audio_pipelines_info == NULL) {
        ESP_LOGE(TAG, "Audio pipeline info is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // === SEND PIPELINE: I2S MIC -> UDP ===
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipelines_info->pipeline_send = audio_pipeline_init(&pipeline_cfg);
    if (audio_pipelines_info->pipeline_send == NULL) {
        ESP_LOGE(TAG, "Failed to initialize send pipeline");
        return ESP_FAIL;
    }

    // === I2S CONFIG
    i2s_stream_cfg_t i2s_cfg_send = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg_send.type = AUDIO_STREAM_READER;
    i2s_cfg_send.chan_cfg.id = CODEC_ADC_I2S_PORT;
    i2s_cfg_send.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg_send.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_cfg_send.std_cfg.clk_cfg.sample_rate_hz = I2S_SAMPLE_RATE;
    i2s_cfg_send.std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    i2s_cfg_send.buffer_len = 324;
    i2s_cfg_send.out_rb_size = 1024; // Increase buffer to avoid missing data in bad network conditions
    i2s_cfg_send.use_alc = true;      // Enable ALC for volume control
    i2s_cfg_send.volume = 30;         // Boost microphone signal by +40dB for small mic
    i2s_cfg_send.task_core = 1; // Run on core 1 to avoid conflicts with other tasks
    audio_pipelines_info->i2s_reader = i2s_stream_init(&i2s_cfg_send);
    if (audio_pipelines_info->i2s_reader == NULL) {
        ESP_LOGE(TAG, "Failed to initialize I2S reader");
        return ESP_FAIL;
    }

    // UDP Audio Element Config
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = audio_pipelines_info->remote_addr,
        .sin_port = htons(UDP_PORT_LOCAL),
    };

    udp_stream_cfg_t udp_cfg_send = {
        .type = AUDIO_STREAM_WRITER,
        .dest_addr = dest_addr,
        .out_rb_size = 2 * 1024,
        .buffer_len = 324,
        .task_stack = 4096
    };
    audio_pipelines_info->udp_writer = udp_stream_init(&udp_cfg_send);
    if (audio_pipelines_info->udp_writer == NULL) {
        ESP_LOGE(TAG, "Failed to initialize UDP writer");
        return ESP_FAIL;
    }

    audio_pipeline_register(audio_pipelines_info->pipeline_send, audio_pipelines_info->i2s_reader, "i2s_reader");
    audio_pipeline_register(audio_pipelines_info->pipeline_send, audio_pipelines_info->udp_writer, "udp_writer");
    const char *link_tag[2] = {"i2s_reader", "udp_writer"};
    audio_pipeline_link(audio_pipelines_info->pipeline_send, link_tag, 2);

    // === RECEIVE PIPELINE: UDP -> I2S SPEAKER ===
    audio_pipelines_info->pipeline_recv = audio_pipeline_init(&pipeline_cfg);
    if (audio_pipelines_info->pipeline_recv == NULL) {
        ESP_LOGE(TAG, "Failed to initialize receive pipeline");
        return ESP_FAIL;
    }

    udp_stream_cfg_t udp_cfg_recv = {
        .type = AUDIO_STREAM_READER,
        .out_rb_size = 2 * 1024,
        .dest_addr = dest_addr,
        .buffer_len = 324,
        .task_stack = 4096
    };
    audio_pipelines_info->udp_reader = udp_stream_init(&udp_cfg_recv);
    if (audio_pipelines_info->udp_reader == NULL) {
        ESP_LOGE(TAG, "Failed to initialize UDP reader");
        return ESP_FAIL;
    }

    i2s_stream_cfg_t i2s_cfg_recv = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg_recv.type = AUDIO_STREAM_WRITER;
    i2s_cfg_recv.chan_cfg.id = CODEC_ADC_I2S_PORT;
    i2s_cfg_recv.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg_recv.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_cfg_recv.std_cfg.clk_cfg.sample_rate_hz = I2S_SAMPLE_RATE;
    i2s_cfg_recv.std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
    i2s_cfg_recv.use_alc = true; // Enable ALC for volume control
    i2s_cfg_recv.volume = 30;
    i2s_cfg_recv.buffer_len = 324;
    i2s_cfg_recv.out_rb_size = 1024; // Increase buffer to avoid missing data in bad network conditions

    audio_pipelines_info->i2s_writer = i2s_stream_init(&i2s_cfg_recv);
    if (audio_pipelines_info->i2s_writer == NULL) {
        ESP_LOGE(TAG, "Failed to initialize I2S writer");
        return ESP_FAIL;
    }

    audio_pipeline_register(audio_pipelines_info->pipeline_recv, audio_pipelines_info->udp_reader, "udp_reader");
    audio_pipeline_register(audio_pipelines_info->pipeline_recv, audio_pipelines_info->i2s_writer, "i2s_writer");
    audio_pipeline_link(audio_pipelines_info->pipeline_recv, (const char *[]) {"udp_reader", "i2s_writer"}, 2);

    ESP_LOGI(TAG, "Audio pipelines initialized successfully");
    return ESP_OK;
}

void audio_pipeline_cleanup(struct audio_pipeline_manager_info *audio_pipelines_info)
{
    if (audio_pipelines_info == NULL) {
        ESP_LOGW(TAG, "Audio pipeline info is NULL, nothing to cleanup");
        return;
    }

    ESP_LOGI(TAG, "Starting audio pipeline cleanup...");
    
    // === CLEANUP SEND PIPELINE ===
    if (audio_pipelines_info->pipeline_send != NULL) {
        ESP_LOGI(TAG, "Cleaning up send pipeline...");
        
        // Force terminate the pipeline instead of graceful stop
        ESP_LOGI(TAG, "Terminating send pipeline...");
        audio_pipeline_terminate(audio_pipelines_info->pipeline_send);
        
        // Unregister send pipeline elements
        if (audio_pipelines_info->i2s_reader != NULL) {
            audio_pipeline_unregister(audio_pipelines_info->pipeline_send, audio_pipelines_info->i2s_reader);
        }
        if (audio_pipelines_info->udp_writer != NULL) {
            audio_pipeline_unregister(audio_pipelines_info->pipeline_send, audio_pipelines_info->udp_writer);
        }
        
        // Deinitialize send pipeline
        audio_pipeline_deinit(audio_pipelines_info->pipeline_send);
        audio_pipelines_info->pipeline_send = NULL;
    }
    
    // === CLEANUP RECEIVE PIPELINE ===
    if (audio_pipelines_info->pipeline_recv != NULL) {
        ESP_LOGI(TAG, "Cleaning up receive pipeline...");
        
        // Force pipeline termination
        ESP_LOGI(TAG, "Terminating receive pipeline...");
        audio_pipeline_terminate(audio_pipelines_info->pipeline_recv);
        
        // Unregister receive pipeline elements
        if (audio_pipelines_info->udp_reader != NULL) {
            audio_pipeline_unregister(audio_pipelines_info->pipeline_recv, audio_pipelines_info->udp_reader);
        }
        if (audio_pipelines_info->i2s_writer != NULL) {
            audio_pipeline_unregister(audio_pipelines_info->pipeline_recv, audio_pipelines_info->i2s_writer);
        }
        
        // Deinitialize receive pipeline
        audio_pipeline_deinit(audio_pipelines_info->pipeline_recv);
        audio_pipelines_info->pipeline_recv = NULL;
    }
    
    // Reset remote address
    audio_pipelines_info->remote_addr = 0;
    
    ESP_LOGI(TAG, "Audio pipeline cleanup completed");
}