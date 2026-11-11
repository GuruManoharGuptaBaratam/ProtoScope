idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        proto_board
        proto_console
        proto_platform
        proto_tasks
)
