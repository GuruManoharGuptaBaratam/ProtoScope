idf_component_register(
    SRCS "proto_rs485.c" "proto_rs485_timing.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_timer proto_board proto_uart
)
