idf_component_register(
    SRCS "proto_platform.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_app_format esp_pm esp_psram esp_system esp_timer nvs_flash spi_flash
)
