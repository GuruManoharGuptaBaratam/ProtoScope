#include "proto_storage.h"

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "proto_board.h"
#include "proto_driver.h"
#include "proto_route_runtime.h"
#include "proto_uibus.h"
#include "sdmmc_cmd.h"

#define MOUNT_POINT "/sdcard"
#define HANDOFF_DEPTH 64
#define LOGGER_DEPTH 64
#define SD_FREQ_KHZ 10000

static const char *TAG = "storage";
static const char *kFirmware = "0.9.1";

static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_handoff;
static proto_logger_t s_logger;
static proto_event_t s_slots[LOGGER_DEPTH];
static sdmmc_card_t *s_card;
static FILE *s_file;
static bool s_mounted;
static bool s_force_remount;
static bool s_monitor;
static volatile uint32_t s_observer_dropped;
static volatile bool s_logging_requested;   /* observer ignores everything until logging is requested */
static bool s_started;

/* ---- SD sink ---------------------------------------------------------------------- */
static void unmount_card(void)
{
    if (s_file) { fclose(s_file); s_file = NULL; }
    if (s_mounted) {
        (void)esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_mounted = false;
        s_card = NULL;
    }
}

static int ensure_mounted(void)
{
    if (s_mounted && s_force_remount) {
        unmount_card();
        s_force_remount = false;
    }
    if (s_mounted) { return PROTO_LOG_OK; }
    if (proto_uibus_init() != ESP_OK) { return PROTO_LOG_ERR_IO; }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = PROTO_UIBUS_HOST;
    host.max_freq_khz = SD_FREQ_KHZ;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PROTO_PIN_SD_CS;
    slot.host_id = PROTO_UIBUS_HOST;
    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,    /* never format a user's card */
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    const esp_err_t err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s (no card, wrong format or wiring)", esp_err_to_name(err));
        s_card = NULL;
        return PROTO_LOG_ERR_NO_CARD;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted on the UI SPI bus (CS GPIO%d)", PROTO_PIN_SD_CS);
    return PROTO_LOG_OK;
}

static int errno_to_result(void)
{
    return (errno == ENOSPC || errno == EFBIG) ? PROTO_LOG_ERR_FULL : PROTO_LOG_ERR_IO;
}

static int sink_list_max_index(void *context, uint16_t *max_index)
{
    (void)context;
    const int rc = ensure_mounted();
    if (rc != PROTO_LOG_OK) { return rc; }
    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) { s_force_remount = true; return PROTO_LOG_ERR_IO; }
    uint16_t best = 0;
    const struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        uint16_t index = 0;
        if (proto_log_parse_index(entry->d_name, &index) && index > best) { best = index; }
    }
    closedir(dir);
    *max_index = best;
    return PROTO_LOG_OK;
}

static int sink_open(void *context, const char *name)
{
    (void)context;
    char path[40];
    snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);
    s_file = fopen(path, "wb");
    if (!s_file) {
        s_force_remount = true;
        return errno == ENOSPC ? PROTO_LOG_ERR_FULL : PROTO_LOG_ERR_IO;
    }
    ESP_LOGI(TAG, "Logging to %s", path);
    return PROTO_LOG_OK;
}

static int sink_write(void *context, const char *data, size_t length)
{
    (void)context;
    if (!s_file) { return PROTO_LOG_ERR_IO; }
    if (fwrite(data, 1, length, s_file) != length) {
        const int rc = errno_to_result();
        if (rc != PROTO_LOG_ERR_FULL) { s_force_remount = true; }
        return rc;
    }
    return PROTO_LOG_OK;
}

static int sink_flush(void *context)
{
    (void)context;
    if (!s_file) { return PROTO_LOG_ERR_IO; }
    if (fflush(s_file) != 0 || fsync(fileno(s_file)) != 0) {
        const int rc = errno_to_result();
        if (rc != PROTO_LOG_ERR_FULL) { s_force_remount = true; }
        return rc;
    }
    return PROTO_LOG_OK;
}

static void sink_close(void *context)
{
    (void)context;
    if (s_file) { fclose(s_file); s_file = NULL; }
}

/* ---- observer hand-off: never blocks the caller -------------------------------------- */
static void on_event(const proto_event_t *event)
{
    if (!s_logging_requested) { return; }
    if (!s_handoff || xQueueSend(s_handoff, event, 0) != pdTRUE) { s_observer_dropped++; }
}

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static void drain_unrouted_sources(void)
{
    static const proto_protocol_t sources[] = { PROTO_PROTOCOL_UART, PROTO_PROTOCOL_RS485, PROTO_PROTOCOL_CAN };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        const proto_driver_t *driver = proto_driver_get(sources[i]);
        if (!driver || !driver->receive || !driver->is_active() ||
            proto_route_runtime_source_in_use(sources[i])) {
            continue;
        }
        proto_event_t event;
        for (int n = 0; n < 8 && driver->receive(&event) == ESP_OK; ++n) { /* observer logs it */ }
    }
}

static void storage_task(void *context)
{
    (void)context;
    proto_event_t event;
    for (;;) {
        const bool got = xQueueReceive(s_handoff, &event, pdMS_TO_TICKS(20)) == pdTRUE;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (got) {
            do { (void)proto_logger_submit(&s_logger, &event); }
            while (xQueueReceive(s_handoff, &event, 0) == pdTRUE);
        }
        proto_logger_service(&s_logger, now_us());
        const bool monitor = s_monitor;
        xSemaphoreGive(s_mutex);
        if (monitor && s_logging_requested) { drain_unrouted_sources(); }
    }
}

esp_err_t proto_storage_start(void)
{
    if (s_started) { return ESP_OK; }
    s_mutex = xSemaphoreCreateMutex();
    s_handoff = xQueueCreate(HANDOFF_DEPTH, sizeof(proto_event_t));
    if (!s_mutex || !s_handoff) { return ESP_ERR_NO_MEM; }
    const proto_log_sink_t sink = { NULL, sink_list_max_index, sink_open, sink_write, sink_flush, sink_close };
    proto_logger_init(&s_logger, &sink, kFirmware, s_slots, LOGGER_DEPTH);
    if (xTaskCreate(storage_task, "proto_storage", 6144, NULL, 4, NULL) != pdPASS) { return ESP_ERR_NO_MEM; }
    proto_event_set_observer(on_event);
    s_started = true;
    ESP_LOGI(TAG, "Logger ready (card is not touched until logging is started)");
    return ESP_OK;
}

proto_log_state_t proto_storage_log_start(void)
{
    if (!s_started) { return PROTO_LOG_STATE_OFF; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_logging_requested = true;
    (void)proto_logger_start(&s_logger, now_us());
    const proto_log_state_t state = s_logger.state;
    xSemaphoreGive(s_mutex);
    return state;
}

void proto_storage_log_stop(void)
{
    if (!s_started) { return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_logging_requested = false;
    proto_logger_stop(&s_logger, now_us());
    xSemaphoreGive(s_mutex);
}

void proto_storage_set_protocol_mask(uint32_t mask)
{
    if (!s_started) { return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_logger.protocol_mask = mask & PROTO_LOG_MASK_ALL;
    xSemaphoreGive(s_mutex);
}

void proto_storage_set_monitor_mode(bool enabled)
{
    if (!s_started) { return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_monitor = enabled;
    xSemaphoreGive(s_mutex);
}

proto_storage_snapshot_t proto_storage_get_snapshot(void)
{
    proto_storage_snapshot_t s;
    memset(&s, 0, sizeof(s));
    if (!s_started) { return s; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s.state = s_logger.state;
    s.wanted = s_logger.wanted;
    s.card_mounted = s_mounted;
    s.monitor_mode = s_monitor;
    s.protocol_mask = s_logger.protocol_mask;
    strncpy(s.file_name, s_logger.file_name, sizeof(s.file_name) - 1);
    s.file_bytes = s_logger.file_bytes;
    s.records_written = s_logger.records_written;
    s.files_created = s_logger.files_created;
    s.write_errors = s_logger.write_errors;
    s.dropped_observer = s_observer_dropped;
    s.dropped_total = proto_logger_dropped_total(&s_logger) + s_observer_dropped;
    s.queue_depth = s_logger.queue.count;
    s.queue_high_water = s_logger.queue.high_water;
    xSemaphoreGive(s_mutex);
    return s;
}
