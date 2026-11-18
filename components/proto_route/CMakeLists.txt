idf_component_register(
    SRCS "proto_route.c" "proto_route_engine.c" "proto_route_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer proto_event
)
