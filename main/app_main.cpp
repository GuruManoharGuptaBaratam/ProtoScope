#include "esp_err.h"
#include "esp_log.h"

#include "proto_board.h"
#include "proto_console.h"
#include "proto_platform.h"
#include "proto_tasks.h"
#include "proto_selftest.h"  // SELFTEST (temporary): remove with proto_selftest.c/.h

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
    ESP_ERROR_CHECK(proto_selftest_register());  // SELFTEST (temporary): adds the `selftest` command

    ESP_LOGI(kTag, "ProtoScope Phase 1 foundation ready");
    ESP_LOGI(kTag, "Phase 2 UART available; stopped until 'uart start'");
    ESP_LOGI(kTag, "Phase 3 CAN available; stopped until 'can start'");
    ESP_LOGI(kTag, "Phase 4 RS-485 available; stopped until 'rs485 start'");
    ESP_LOGI(kTag, "Phase 5 I2C/SPI controllers available; stopped until 'i2c start' / 'spi start'");
    ESP_LOGI(kTag, "Phase 6 common events available through 'msg'");
    ESP_LOGI(kTag, "Phase 7 routing engine available through 'route'; all 20 routes disabled");
    ESP_LOGI(kTag, "Phase 8 monitor and microSD logger available through 'monitor' and 'log'");
    ESP_LOGI(kTag, "Phase 9 display and encoder UI available through 'display' and 'ui'");
}
