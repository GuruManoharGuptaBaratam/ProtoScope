idf_component_register(
    SRCS "proto_i2c.c" "proto_i2c_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c proto_board
)
