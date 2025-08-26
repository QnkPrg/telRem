#include "peripheral_manager.h"
#include "../network/wifi_provisioning.h"
#include "../control/device_manager.h"
#include "esp_log.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "input_key_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PRESS_DURATION_MS 3000  // 3 seconds for long press
#define WAIT_TIME_BETWEEN_NOTIFICATIONS_MS 5000  // 5 seconds between notifications

static const char *TAG = "PERIPHERAL_MANAGER";

// Forward declaration
static esp_err_t _input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx);

esp_err_t peripheral_manager_init(void)
{
    esp_periph_set_handle_t periph_set = NULL;
    periph_service_handle_t input_key_service = NULL;

    ESP_LOGI(TAG, "Initializing peripheral manager...");
    
    // Initialize peripheral set
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_set = esp_periph_set_init(&periph_cfg);
    if (periph_set == NULL) {
        ESP_LOGE(TAG, "Failed to initialize peripheral set");
        return ESP_FAIL;
    }
    
    // Initialize audio board
    audio_board_handle_t board_handle = audio_board_init();
    if (board_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize audio board");
        return ESP_FAIL;
    }
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    
    // Initialize button peripheral
    audio_board_key_init(periph_set);
    
    // Configure input key service for button monitoring
    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    input_cfg.handle = periph_set;
    input_key_service = input_key_service_create(&input_cfg);
    if (input_key_service == NULL) {
        ESP_LOGE(TAG, "Failed to create input key service");
        return ESP_FAIL;
    }
    
    input_key_service_add_key(input_key_service, input_key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_key_service, _input_key_service_cb, (void *)board_handle);
    
    ESP_LOGI(TAG, "Peripheral manager initialized: audio board and buttons");
    return ESP_OK;
}

static esp_err_t _input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    static uint32_t button_press_start_time[INPUT_KEY_NUM] = {0};
    static uint32_t last_doorbell_ring_time = 0;
    static bool button_pressed[INPUT_KEY_NUM] = {false};
    int button_id = (int)evt->data;
    
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK) {
        // Button pressed - start timing
        if (button_id < INPUT_KEY_NUM) {
            button_press_start_time[button_id] = xTaskGetTickCount();
            button_pressed[button_id] = true;
        }
    }
    else if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE || evt->type == INPUT_KEY_SERVICE_ACTION_PRESS_RELEASE) {
        // Button released - check duration
        if (button_id < INPUT_KEY_NUM && button_pressed[button_id]) {
            uint32_t now = xTaskGetTickCount();
            uint32_t press_duration = (now - button_press_start_time[button_id]) * portTICK_PERIOD_MS;
            button_pressed[button_id] = false;

            switch(button_id) {
                case INPUT_KEY_USER_ID_REC:
                    if (last_doorbell_ring_time == 0 ||
                        (now - last_doorbell_ring_time) * portTICK_PERIOD_MS >= WAIT_TIME_BETWEEN_NOTIFICATIONS_MS) {
                        ESP_LOGI(TAG, "Doorbell button released - notifying all clients");
                        broadcast_doorbell_ring();
                        last_doorbell_ring_time = xTaskGetTickCount();
                    }
                    break;
                case INPUT_KEY_USER_ID_PLAY:
                    if (press_duration >= BUTTON_PRESS_DURATION_MS) {
                        ESP_LOGI(TAG, "WiFi reset button released - resetting WiFi provisioning");
                        // NON RETURNING: Trigger WiFi reset
                        // This will clear WiFi credentials and restart the device
                        clear_wifi_provisioning();
                    } else {
                        ESP_LOGI(TAG, "Short press on WiFi reset button - no action taken");
                    }
                    break;
                default:
                    break;
            }
        }
    }
    
    return ESP_OK;
}
