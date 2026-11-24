#include "proto_platform.h"

#include <inttypes.h>
#include <stdbool.h>

#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "platform";

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires reformatting during Phase-1 initialization");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t proto_platform_init(void)
{
    esp_log_level_set("*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "NVS initialization failed");

#if CONFIG_PM_ENABLE
    const esp_pm_config_t power_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    ESP_RETURN_ON_ERROR(esp_pm_configure(&power_config), TAG,
                        "Dynamic frequency configuration failed");
    ESP_LOGI(TAG, "Power management: CPU %d-80 MHz, light sleep disabled",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
#else
    ESP_LOGW(TAG, "Dynamic power management is disabled in sdkconfig");
#endif

    return ESP_OK;
}

void proto_platform_log_boot_summary(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);
    const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "ESP-IDF %s; ESP32-S3 rev %d, %d core(s)",
             esp_get_idf_version(), chip_info.revision, chip_info.cores);
    ESP_LOGI(TAG, "ProtoScope firmware %s; board memory profile N16R8",
             esp_app_get_description()->version);
    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024U * 1024U));
    } else {
        ESP_LOGW(TAG, "Flash-size query failed: %s", esp_err_to_name(flash_result));
    }
    if (esp_psram_is_initialized()) {
        ESP_LOGI(TAG, "PSRAM: %u bytes", (unsigned)esp_psram_get_size());
    } else {
        ESP_LOGW(TAG, "PSRAM is not initialized; verify the selected board variant");
    }
    ESP_LOGI(TAG, "Reset reason: %d; free heap: %u bytes; minimum free heap: %u bytes",
             (int)esp_reset_reason(),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

void proto_platform_print_status(FILE *stream)
{
    const int64_t uptime_ms = esp_timer_get_time() / 1000;
    fprintf(stream, "ProtoScope status\n");
    fprintf(stream, "  firmware: %s\n", esp_app_get_description()->version);
    fprintf(stream, "  ESP-IDF: %s\n", esp_get_idf_version());
    fprintf(stream, "  uptime_ms: %" PRId64 "\n", uptime_ms);
    fprintf(stream, "  cpu_max_mhz: %d\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    fprintf(stream, "  free_heap_bytes: %u\n", (unsigned)esp_get_free_heap_size());
    fprintf(stream, "  minimum_free_heap_bytes: %u\n",
            (unsigned)esp_get_minimum_free_heap_size());
    fprintf(stream, "  psram_initialized: %s\n", esp_psram_is_initialized() ? "yes" : "no");
    fprintf(stream, "  psram_size_bytes: %u\n", (unsigned)esp_psram_get_size());
    fprintf(stream, "  protocol_drivers: all interfaces start stopped; status below (UI/display start at boot)\n");
}
