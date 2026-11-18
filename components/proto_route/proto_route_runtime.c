#include "proto_route_runtime.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "proto_driver.h"

static const char *TAG = "route";
static proto_route_engine_t s_engine;
static SemaphoreHandle_t s_lock;
static bool s_started;

static bool io_is_active(void *context, proto_protocol_t protocol)
{
    (void)context;
    const proto_driver_t *driver = proto_driver_get(protocol);
    return driver && driver->is_active();
}

static proto_io_status_t io_receive(void *context, proto_protocol_t protocol, proto_event_t *event)
{
    (void)context;
    const proto_driver_t *driver = proto_driver_get(protocol);
    if (!driver || !driver->receive) { return PROTO_IO_EMPTY; }
    const esp_err_t err = driver->receive(event);
    if (err == ESP_OK) { return PROTO_IO_OK; }
    return err == ESP_ERR_NOT_FOUND ? PROTO_IO_EMPTY : PROTO_IO_FAILED;
}

static proto_io_status_t io_submit(void *context, proto_protocol_t protocol,
                                   const proto_event_t *request, proto_event_t *result,
                                   int32_t *error)
{
    (void)context;
    const proto_driver_t *driver = proto_driver_get(protocol);
    if (!driver || !driver->submit) {
        *error = (int32_t)ESP_ERR_NOT_SUPPORTED;
        return PROTO_IO_FAILED;
    }
    const esp_err_t err = driver->submit(request, result);
    *error = (int32_t)err;
    if (err == ESP_OK) { return PROTO_IO_OK; }
    /* A full TX queue / busy interface is retryable; everything else is a failure. */
    return err == ESP_ERR_TIMEOUT ? PROTO_IO_BUSY : PROTO_IO_FAILED;
}

static uint64_t io_now_us(void *context)
{
    (void)context;
    return (uint64_t)esp_timer_get_time();
}

static uint64_t io_next_id(void *context)
{
    (void)context;
    return proto_event_next_id();
}

static void route_task(void *context)
{
    (void)context;
    const proto_route_io_t io = {
        .context = NULL, .is_active = io_is_active, .receive = io_receive,
        .submit = io_submit, .now_us = io_now_us, .next_id = io_next_id,
    };
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        proto_route_engine_step(&s_engine, &io);
        xSemaphoreGive(s_lock);
        vTaskDelay(1);
    }
}

esp_err_t proto_route_runtime_start(void)
{
    if (s_started) { return ESP_OK; }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) { return ESP_ERR_NO_MEM; }
    proto_route_engine_init(&s_engine);
    if (xTaskCreate(route_task, "proto_route", 6144, NULL, 5, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "Routing engine ready: 20 routes defined, all disabled");
    return ESP_OK;
}

void proto_route_runtime_lock(void)
{
    if (s_lock) { xSemaphoreTake(s_lock, portMAX_DELAY); }
}

void proto_route_runtime_unlock(void)
{
    if (s_lock) { xSemaphoreGive(s_lock); }
}

proto_route_engine_t *proto_route_runtime_engine(void)
{
    return &s_engine;
}
