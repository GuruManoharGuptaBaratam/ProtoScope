idf_component_register(
    SRCS "proto_uibus.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi proto_board
)
