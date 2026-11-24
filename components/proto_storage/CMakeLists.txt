idf_component_register(
    SRCS "proto_log_format.c" "proto_log_queue.c" "proto_logger.c" "proto_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer fatfs sdmmc esp_driver_sdspi esp_driver_spi vfs proto_board proto_uibus
             proto_event proto_route
)
