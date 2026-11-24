idf_component_register(
    SRCS "proto_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES heap esp_timer esp_app_format proto_uart proto_rs485 proto_twai proto_i2c proto_spi
             proto_route proto_storage proto_event
)
