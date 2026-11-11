#include "proto_tasks.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "tasks";

typedef enum {
    PROTO_WORK_HEALTH_TICK = 1,
} proto_work_kind_t;

typedef struct {
    proto_work_kind_t kind;
    int64_t created_us;
} proto_work_item_t;

static QueueHandle_t s_work_queue;
static TaskHandle_t s_worker_task;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static proto_tasks_snapshot_t s_snapshot;

static void worker_task(void *context)
{
    (void)context;
    proto_work_item_t item;

    for (;;) {
        if (xQueueReceive(s_work_queue, &item, portMAX_DELAY) == pdTRUE) {
            portENTER_CRITICAL(&s_snapshot_lock);
            ++s_snapshot.worker_messages;
            portEXIT_CRITICAL(&s_snapshot_lock);

            if (item.kind == PROTO_WORK_HEALTH_TICK) {
                ESP_LOGD(TAG, "Worker handled health tick after %" PRId64 " us",
                         esp_timer_get_time() - item.created_us);
            }
        }
    }
}

static void supervisor_task(void *context)
{
    (void)context;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_PROTOSCOPE_HEALTH_PERIOD_MS);

    for (;;) {
        vTaskDelay(period);

        const proto_work_item_t item = {
            .kind = PROTO_WORK_HEALTH_TICK,
            .created_us = esp_timer_get_time(),
        };
        if (xQueueSend(s_work_queue, &item, 0) != pdTRUE) {
            portENTER_CRITICAL(&s_snapshot_lock);
            ++s_snapshot.queue_overflows;
            portEXIT_CRITICAL(&s_snapshot_lock);
            ESP_LOGW(TAG, "Runtime work queue full");
        }

        portENTER_CRITICAL(&s_snapshot_lock);
        ++s_snapshot.supervisor_cycles;
        const uint32_t cycle = s_snapshot.supervisor_cycles;
        portEXIT_CRITICAL(&s_snapshot_lock);

        ESP_LOGI(TAG,
                 "health cycle=%" PRIu32 " uptime_ms=%" PRId64
                 " heap=%u supervisor_stack_min=%u",
                 cycle, esp_timer_get_time() / 1000,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

esp_err_t proto_tasks_start(void)
{
    if (s_work_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_work_queue = xQueueCreate(8, sizeof(proto_work_item_t));
    if (s_work_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(worker_task, "proto_worker", 4096, NULL, 5, &s_worker_task) != pdPASS) {
        vQueueDelete(s_work_queue);
        s_work_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(supervisor_task, "proto_supervisor", 4096, NULL, 4, NULL) != pdPASS) {
        vTaskDelete(s_worker_task);
        s_worker_task = NULL;
        vQueueDelete(s_work_queue);
        s_work_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "FreeRTOS runtime started: worker priority 5, supervisor priority 4");
    return ESP_OK;
}

proto_tasks_snapshot_t proto_tasks_get_snapshot(void)
{
    proto_tasks_snapshot_t copy;
    portENTER_CRITICAL(&s_snapshot_lock);
    copy = s_snapshot;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return copy;
}
