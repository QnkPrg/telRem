set(COMPONENT_SRCS "main.c" 
                  "network/wifi_provisioning.c"
                  "network/mdns_service.c"
                  "audio/audio_pipeline_manager.c" 
                  "control/device_manager.c"
                  "peripheral/peripheral_manager.c"
                  "video/video_manager.c")
set(COMPONENT_ADD_INCLUDEDIRS . network audio control peripheral video)

set(COMPONENT_REQUIRES esp_http_server json nvs_flash driver audio_pipeline audio_stream audio_hal audio_board esp_peripherals input_key_service mdns esp32-camera)

register_component()