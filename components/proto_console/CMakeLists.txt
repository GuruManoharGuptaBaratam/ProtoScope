idf_component_register(
    SRCS "proto_console.c" "proto_uart_console.c" "proto_can_console.c" "proto_rs485_console.c"
    INCLUDE_DIRS "include"
    REQUIRES console esp_system proto_platform proto_tasks proto_uart proto_twai proto_rs485
)
