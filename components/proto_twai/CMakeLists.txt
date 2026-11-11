idf_component_register(
    SRCS "proto_can.c" "proto_can_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_twai esp_timer proto_board
)
