idf_component_register(
    SRCS "proto_canvas.c" "proto_font5x7.c" "proto_encoder_decode.c" "proto_ui_model.c"
         "proto_display.c" "proto_encoder.c" "proto_ui_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_timer heap
             proto_board proto_monitor proto_storage proto_route proto_event
             proto_uart proto_rs485 proto_twai proto_i2c proto_spi
)
