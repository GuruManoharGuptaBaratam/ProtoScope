#include "proto_console.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "proto_platform.h"
#include "proto_tasks.h"
#include "proto_uart_console.h"
#include "sdkconfig.h"

static const char *TAG = "console";

#if CONFIG_PROTOSCOPE_CONSOLE_ENABLE
static esp_console_repl_t *s_repl;

static int status_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    proto_platform_print_status(stdout);
    const proto_tasks_snapshot_t tasks = proto_tasks_get_snapshot();
    printf("  supervisor_cycles: %" PRIu32 "\n", tasks.supervisor_cycles);
    printf("  worker_messages: %" PRIu32 "\n", tasks.worker_messages);
    printf("  runtime_queue_overflows: %" PRIu32 "\n", tasks.queue_overflows);
    proto_uart_console_print_status();
    return 0;
}

static int heap_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("free_heap_bytes=%u minimum_free_heap_bytes=%u\n",
           (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size());
    return 0;
}

static int reboot_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Restarting ProtoScope...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

static esp_err_t register_commands(void)
{
    const esp_console_cmd_t status = {
        .command = "status",
        .help = "Show platform, runtime and UART status",
        .hint = NULL,
        .func = &status_command,
        .argtable = NULL,
    };
    const esp_console_cmd_t heap = {
        .command = "heap",
        .help = "Show current and minimum free heap",
        .hint = NULL,
        .func = &heap_command,
        .argtable = NULL,
    };
    const esp_console_cmd_t reboot = {
        .command = "reboot",
        .help = "Restart the ESP32-S3",
        .hint = NULL,
        .func = &reboot_command,
        .argtable = NULL,
    };

    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG,
                        "Help command registration failed");
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&status), TAG,
                        "Status command registration failed");
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&heap), TAG,
                        "Heap command registration failed");
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&reboot), TAG,
                        "Reboot command registration failed");
    ESP_RETURN_ON_ERROR(proto_uart_console_register(), TAG,
                        "UART command registration failed");
    return ESP_OK;
}
#endif

esp_err_t proto_console_start(void)
{
#if CONFIG_PROTOSCOPE_CONSOLE_ENABLE
    ESP_RETURN_ON_ERROR(register_commands(), TAG, "Console command setup failed");

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "protoscope> ";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&uart_config, &repl_config, &s_repl),
                        TAG, "UART REPL creation failed");
    ESP_RETURN_ON_ERROR(esp_console_start_repl(s_repl), TAG, "UART REPL start failed");
    ESP_LOGI(TAG, "Interactive console ready on UART%d at %d baud",
             CONFIG_ESP_CONSOLE_UART_NUM, CONFIG_ESP_CONSOLE_UART_BAUDRATE);
#else
    ESP_LOGW(TAG, "Interactive console disabled by sdkconfig");
#endif
    return ESP_OK;
}
