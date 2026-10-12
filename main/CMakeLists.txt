idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        esp_driver_gpio
        esp_driver_ledc
        esp_timer
        mqtt
        nvs_flash
)