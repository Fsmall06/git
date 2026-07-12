/**
 * @file c5_runtime_workers.c
 * @brief C5 runtime worker 实现，承接 CSI、BME 和 system 事件。
 *
 * worker 层是 C5 runtime 的业务执行边界：event bus handler 只把事件投递到
 * 对应 worker queue，worker 再调用各 domain service 的 tick 函数。
 */

#include "c5_runtime_workers.h"

#include <stdint.h>

#include "bme_sensor_service.h"
#include "c5_backpressure_controller.h"
#include "c5_event_bus.h"
#include "csi_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "system_service.h"

static const char *TAG = "c5_workers";

static QueueHandle_t s_csi_worker_queue;
static QueueHandle_t s_bme_worker_queue;
static QueueHandle_t s_system_worker_queue;
static TaskHandle_t s_csi_worker_task;
static TaskHandle_t s_bme_worker_task;
static TaskHandle_t s_system_worker_task;
static uint64_t s_csi_next_report_ms;

static uint64_t c5_worker_now_ms(void)
{
    int64_t now_us = esp_timer_get_time();
    return now_us > 0 ? (uint64_t)(now_us / 1000) : 0U;
}

static void c5_worker_record_latency(const c5_event_t *event)
{
    if (event == NULL) {
        return;
    }

    /* 延迟统计只反映排队等待时间，便于判断 dispatcher/worker 是否出现积压。 */
    uint64_t now_ms = c5_worker_now_ms();
    uint32_t latency_ms = now_ms >= event->timestamp_ms ?
                          (uint32_t)(now_ms - event->timestamp_ms) :
                          0U;
    c5_event_bus_record_worker_latency(latency_ms);
}

static void c5_worker_log_ret(const char *worker, const c5_event_t *event, esp_err_t ret)
{
    if (ret == ESP_OK ||
        ret == ESP_ERR_INVALID_STATE ||
        ret == ESP_ERR_NOT_FOUND) {
        return;
    }

    ESP_LOGW(TAG,
             "%s failed event=%s source=%s ret=%s",
             worker,
             event != NULL ? c5_event_type_name(event->type) : "unknown",
             event != NULL ? c5_event_source_name(event->source) : "unknown",
             esp_err_to_name(ret));
}

static esp_err_t c5_worker_enqueue(QueueHandle_t queue, const c5_event_t *event)
{
    if (queue == NULL || event == NULL) {
        c5_event_bus_note_drop();
        return ESP_ERR_INVALID_STATE;
    }
    /* 路由阶段不等待 worker queue；队列满说明下游已经积压，直接计 drop。 */
    if (xQueueSend(queue, event, 0) != pdTRUE) {
        c5_event_bus_note_drop();
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t c5_route_csi_event(const c5_event_t *event, void *ctx)
{
    (void)ctx;
    return c5_worker_enqueue(s_csi_worker_queue, event);
}

static esp_err_t c5_route_bme_event(const c5_event_t *event, void *ctx)
{
    (void)ctx;
    return c5_worker_enqueue(s_bme_worker_queue, event);
}

static esp_err_t c5_route_system_event(const c5_event_t *event, void *ctx)
{
    (void)ctx;
    return c5_worker_enqueue(s_system_worker_queue, event);
}

static void csi_worker(void *arg)
{
    (void)arg;
    c5_event_t event = {0};

    while (1) {
        if (xQueueReceive(s_csi_worker_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* CSI 高频 process 和低频 report 分开计时，避免 report cadence 被 process tick 改写。 */
        esp_err_t process_ret = csi_service_process_tick();
        c5_worker_log_ret("csi_worker_process", &event, process_ret);

        uint64_t now_ms = c5_worker_now_ms();
        if (s_csi_next_report_ms == 0U || now_ms >= s_csi_next_report_ms) {
            esp_err_t report_ret = csi_service_report_tick();
            c5_worker_log_ret("csi_worker_report", &event, report_ret);
            s_csi_next_report_ms = now_ms + c5_get_interval(C5_TASK_TYPE_CSI_REPORT);
        }
        c5_worker_record_latency(&event);
    }
}

static void bme_worker(void *arg)
{
    (void)arg;
    c5_event_t event = {0};

    while (1) {
        if (xQueueReceive(s_bme_worker_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t ret = bme_sensor_service_tick();
        c5_worker_log_ret("bme_worker", &event, ret);
        c5_worker_record_latency(&event);
    }
}

static void system_worker(void *arg)
{
    (void)arg;
    c5_event_t event = {0};

    while (1) {
        if (xQueueReceive(s_system_worker_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* system worker 集中处理 heartbeat/status/command poll，保持 C5->S3 系统上报顺序。 */
        esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
        switch (event.type) {
        case C5_EVENT_HEARTBEAT:
            ret = system_service_tick_heartbeat();
            break;
        case C5_EVENT_STATUS:
            ret = system_service_tick_status();
            break;
        case C5_EVENT_COMMAND:
            ret = system_service_tick_command_poll();
            break;
        default:
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
        }
        c5_worker_log_ret("system_worker", &event, ret);
        c5_worker_record_latency(&event);
    }
}

static esp_err_t c5_runtime_workers_create_queue(QueueHandle_t *queue)
{
    if (queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*queue != NULL) {
        return ESP_OK;
    }

    *queue = xQueueCreate((UBaseType_t)C5_WORKER_QUEUE_LENGTH, sizeof(c5_event_t));
    return *queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t c5_runtime_workers_create_task(TaskHandle_t *task,
                                                TaskFunction_t entry,
                                                const char *name)
{
    if (task == NULL || entry == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*task != NULL) {
        return ESP_OK;
    }

    BaseType_t created = xTaskCreate(entry,
                                     name,
                                     C5_WORKER_TASK_STACK,
                                     NULL,
                                     C5_WORKER_TASK_PRIORITY,
                                     task);
    if (created != pdPASS) {
        *task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t c5_runtime_workers_start(void)
{
    esp_err_t ret = c5_event_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }

    /* 三类 worker 拆队列，避免 CSI 高频事件挤占 heartbeat/status/command poll。 */
    ret = c5_runtime_workers_create_queue(&s_csi_worker_queue);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_runtime_workers_create_queue(&s_bme_worker_queue);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_runtime_workers_create_queue(&s_system_worker_queue);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = c5_event_bus_register_handler(C5_EVENT_CSI_READY, c5_route_csi_event, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_event_bus_register_handler(C5_EVENT_BME_SAMPLE, c5_route_bme_event, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_event_bus_register_handler(C5_EVENT_HEARTBEAT, c5_route_system_event, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_event_bus_register_handler(C5_EVENT_STATUS, c5_route_system_event, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_event_bus_register_handler(C5_EVENT_COMMAND, c5_route_system_event, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = c5_runtime_workers_create_task(&s_csi_worker_task, csi_worker, "csi_worker");
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_runtime_workers_create_task(&s_bme_worker_task, bme_worker, "bme_worker");
    if (ret != ESP_OK) {
        return ret;
    }
    ret = c5_runtime_workers_create_task(&s_system_worker_task, system_worker, "system_worker");
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG,
             "C5 workers started priority=%u stack=%u queue_len=%u",
             (unsigned int)C5_WORKER_TASK_PRIORITY,
             (unsigned int)C5_WORKER_TASK_STACK,
             (unsigned int)C5_WORKER_QUEUE_LENGTH);
    return ESP_OK;
}
