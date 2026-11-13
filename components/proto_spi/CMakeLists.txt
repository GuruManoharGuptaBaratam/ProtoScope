idf_component_register(
    SRCS "proto_spi.c" "proto_spi_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi proto_board
)
