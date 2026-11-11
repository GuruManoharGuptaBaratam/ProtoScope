idf_component_register(
    SRCS "proto_console.c"
    INCLUDE_DIRS "include"
    REQUIRES console esp_system proto_platform proto_tasks
)
