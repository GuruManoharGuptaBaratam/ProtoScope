idf_component_register(
    SRCS "proto_event.c" "proto_event_caps.c" "proto_hex.c" "proto_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer proto_uart proto_rs485 proto_twai proto_i2c proto_spi
)
