#include "esp_err.h"
#include "esp_log.h"

#include "proto_board.h"
#include "proto_console.h"
#include "proto_platform.h"
#include "proto_tasks.h"

namespace {
constexpr char kTag[] = "app";
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(proto_platform_init());
    ESP_ERROR_CHECK(proto_board_init_safe());
    ESP_ERROR_CHECK(proto_tasks_start());
    proto_platform_log_boot_summary();
    ESP_ERROR_CHECK(proto_console_start());

    ESP_LOGI(kTag, "ProtoScope Phase 1 foundation ready");
    ESP_LOGI(kTag, "Phase 2 UART available; stopped until 'uart start'");
    ESP_LOGI(kTag, "Phase 3 CAN available; stopped until 'can start'");
}
