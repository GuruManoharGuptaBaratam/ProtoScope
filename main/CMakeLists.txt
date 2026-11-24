idf_component_register(
    SRCS "app_main.cpp"
         "proto_selftest.c"          # SELFTEST (temporary)
    INCLUDE_DIRS "."
    REQUIRES
        proto_board
        proto_console
        proto_platform
        proto_tasks
        # SELFTEST (temporary): everything below is only needed by proto_selftest.c
        proto_uart
        proto_twai
        proto_rs485
        proto_i2c
        proto_spi
        proto_ui
        proto_uibus
        proto_storage
        console
        esp_timer
        fatfs
        sdmmc
        esp_driver_sdspi
        esp_driver_spi
        esp_driver_gpio
)
