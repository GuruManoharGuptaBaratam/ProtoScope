idf_component_register(
    SRCS "proto_uart.c" "proto_uart_framer.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_timer proto_board
)
