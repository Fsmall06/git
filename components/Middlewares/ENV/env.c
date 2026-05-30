#include "env.h"

#include <string.h>

#include "bme690.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ENV";

/* ENV 模块运行状态。
 * 说明：
 *     ENV 自身只保存采样服务需要的状态，不暴露 BSP/BME690 内部细节。
 *     这样后续移植到其它环境传感器时，只需要替换 env_read_sensor_once() 的数据来源。
 */
static env_data_t s_env_latest_data;          /* 最新环境数据缓存，由 ENV 任务写入，外部通过 env_get_data() 读取 */
static SemaphoreHandle_t s_env_data_mutex;    /* 最新环境数据互斥锁，防止任务写入和外部读取同时发生 */
static TaskHandle_t s_env_task_handle;        /* ENV 采样任务句柄，用于判断任务是否已创建 */
static bool s_env_sensor_ready;               /* BME690 是否已初始化成功 */

/* env_get_time_ms：获取系统启动后的毫秒时间。
 *
 * 返回：
 *     当前系统运行时间，单位 ms。
 *
 * 调用方法：
 *     uint64_t now_ms = env_get_time_ms();
 */
static uint64_t env_get_time_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

/* env_delay_ms：ENV 任务毫秒级延时。
 *
 * 参数：
 *     delay_ms：需要延时的时间，单位 ms。
 *
 * 调用方法：
 *     env_delay_ms(ENV_SAMPLE_PERIOD_MS);
 */
static void env_delay_ms(uint32_t delay_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    vTaskDelay(ticks);
}

/* env_lock_data：锁定 ENV 最新数据缓存。
 *
 * 返回：
 *     ESP_OK：锁定成功；
 *     ESP_ERR_INVALID_STATE：互斥锁未创建或锁定失败。
 *
 * 调用方法：
 *     if (env_lock_data() == ESP_OK) {
 *         // 安全访问 s_env_latest_data
 *         env_unlock_data();
 *     }
 */
static esp_err_t env_lock_data(void)
{
    if (s_env_data_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_env_data_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/* env_unlock_data：解锁 ENV 最新数据缓存。
 *
 * 调用方法：
 *     env_unlock_data();
 */
static void env_unlock_data(void)
{
    if (s_env_data_mutex != NULL)
    {
        (void)xSemaphoreGive(s_env_data_mutex);
    }
}

/* env_copy_from_bme690：把 BSP/BME690 数据转换成 ENV 对外数据结构。
 *
 * 参数：
 *     sensor_data：BME690 原始业务数据，不能为 NULL。
 *     env_data：ENV 对外缓存数据，不能为 NULL。
 *
 * 调用方法：
 *     bme690_data_t bme = {0};
 *     env_data_t env = {0};
 *     env_copy_from_bme690(&bme, &env);
 */
static void env_copy_from_bme690(const bme690_data_t *sensor_data, env_data_t *env_data)
{
    env_data->temperature_c = sensor_data->temperature_c;
    env_data->humidity_percent = sensor_data->humidity_percent;
    env_data->pressure_pa = sensor_data->pressure_pa;
    env_data->pressure_hpa = sensor_data->pressure_hpa;
    env_data->gas_resistance_ohm = sensor_data->gas_resistance_ohm;
    env_data->gas_valid = sensor_data->gas_valid;
    env_data->heat_stable = sensor_data->heat_stable;
    env_data->valid = true;
    env_data->timestamp_ms = env_get_time_ms();
}

/* env_update_latest_data：更新 ENV 最新环境数据缓存。
 *
 * 参数：
 *     sensor_data：本次从 BME690 读取到的数据，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：更新成功；
 *     ESP_ERR_INVALID_ARG：sensor_data 为 NULL；
 *     ESP_ERR_INVALID_STATE：数据锁异常。
 *
 * 调用方法：
 *     bme690_data_t sensor_data = {0};
 *     env_update_latest_data(&sensor_data);
 */
static esp_err_t env_update_latest_data(const bme690_data_t *sensor_data)
{
    if (sensor_data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = env_lock_data();
    if (ret != ESP_OK)
    {
        return ret;
    }

    env_data_t next_data = s_env_latest_data;
    env_copy_from_bme690(sensor_data, &next_data);
    next_data.sample_count++;

    s_env_latest_data = next_data;
    env_unlock_data();

    return ESP_OK;
}

/* env_increase_error_count：采样失败计数加 1。
 *
 * 功能：
 *     BME690 初始化或读取失败时调用，用于后续通过 env_get_data() 观察错误次数。
 *
 * 调用方法：
 *     env_increase_error_count();
 */
static void env_increase_error_count(void)
{
    if (env_lock_data() != ESP_OK)
    {
        return;
    }

    s_env_latest_data.error_count++;
    env_unlock_data();
}

/* env_try_init_sensor：尝试初始化 BME690。
 *
 * 返回：
 *     ESP_OK：BME690 已经初始化成功；
 *     其它值：BME690 初始化失败。
 *
 * 调用方法：
 *     esp_err_t ret = env_try_init_sensor();
 */
static esp_err_t env_try_init_sensor(void)
{
    if (s_env_sensor_ready)
    {
        return ESP_OK;
    }

    esp_err_t ret = bme690_init();
    if (ret == ESP_OK)
    {
        s_env_sensor_ready = true;
        ESP_LOGI(TAG, "ENV 传感器初始化成功");
    }
    else
    {
        env_increase_error_count();
        ESP_LOGI(TAG, "ENV 传感器初始化失败，ret: %d，%d ms 后重试", ret, ENV_INIT_RETRY_DELAY_MS);
    }

    return ret;
}

/* env_read_sensor_once：读取一次 BME690 并更新 ENV 缓存。
 *
 * 返回：
 *     ESP_OK：读取并更新成功；
 *     其它值：读取失败。
 *
 * 调用方法：
 *     esp_err_t ret = env_read_sensor_once();
 */
static esp_err_t env_read_sensor_once(void)
{
    bme690_data_t sensor_data = {0};

    esp_err_t ret = bme690_read(&sensor_data);
    if (ret != ESP_OK)
    {
        env_increase_error_count();
        return ret;
    }

    return env_update_latest_data(&sensor_data);
}

/* env_print_data_log：按固定格式打印最新环境数据。
 *
 * 参数：
 *     data：需要打印的环境数据，不能为 NULL。
 *
 * 调用方法：
 *     env_data_t data = {0};
 *     env_print_data_log(&data);
 */
static void env_print_data_log(const env_data_t *data)
{
    if ((data == NULL) || !data->valid)
    {
        ESP_LOGI(TAG, "ENV 数据暂未有效");
        return;
    }

    ESP_LOGI(TAG,
             "环境数据: temp:%.2f C, hum:%.2f %%, press:%.2f hPa, gas:%lu Ohm, gas_valid:%d, heat_stable:%d, samples:%lu, errors:%lu, time:%llu ms",
             data->temperature_c,
             data->humidity_percent,
             data->pressure_hpa,
             (unsigned long)data->gas_resistance_ohm,
             data->gas_valid,
             data->heat_stable,
             (unsigned long)data->sample_count,
             (unsigned long)data->error_count,
             (unsigned long long)data->timestamp_ms);
}

/* env_should_print_periodic_log：判断当前是否到达周期日志打印时间。
 *
 * 参数：
 *     now_ms：当前系统时间，单位 ms。
 *     last_log_ms：上一次日志打印时间的指针，不能为 NULL。
 *
 * 返回：
 *     true：需要打印日志；
 *     false：未到打印时间。
 *
 * 调用方法：
 *     if (env_should_print_periodic_log(now_ms, &last_log_ms)) {
 *         // 打印日志
 *     }
 */
static bool env_should_print_periodic_log(uint64_t now_ms, uint64_t *last_log_ms)
{
    if (last_log_ms == NULL)
    {
        return false;
    }

    if ((now_ms - *last_log_ms) < ENV_LOG_PERIOD_MS)
    {
        return false;
    }

    *last_log_ms = now_ms;
    return true;
}

/* env_should_print_fail_log：判断读取失败日志是否允许打印。
 *
 * 参数：
 *     now_ms：当前系统时间，单位 ms。
 *     last_fail_log_ms：上一次失败日志打印时间的指针，不能为 NULL。
 *
 * 返回：
 *     true：允许打印失败日志；
 *     false：距离上一次失败日志太近，暂不打印。
 *
 * 调用方法：
 *     if (env_should_print_fail_log(now_ms, &last_fail_log_ms)) {
 *         ESP_LOGI(TAG, "读取失败");
 *     }
 */
static bool env_should_print_fail_log(uint64_t now_ms, uint64_t *last_fail_log_ms)
{
    if (last_fail_log_ms == NULL)
    {
        return false;
    }

    if ((now_ms - *last_fail_log_ms) < ENV_READ_FAIL_LOG_PERIOD_MS)
    {
        return false;
    }

    *last_fail_log_ms = now_ms;
    return true;
}

/* env_sample_task：FreeRTOS 环境采样任务。
 *
 * 参数：
 *     arg：任务参数，当前未使用，保留用于后续扩展。
 *
 * 功能：
 *     1. 确保 BME690 初始化成功；
 *     2. 周期读取 BME690 环境数据；
 *     3. 更新 ENV 最新环境数据缓存；
 *     4. 按 ENV_LOG_PERIOD_MS 周期打印环境数据日志。
 *
 * 调用方法：
 *     该函数由 env_init() 内部通过 xTaskCreatePinnedToCore() 创建，不建议外部直接调用。
 */
static void env_sample_task(void *arg)
{
    (void)arg;

    uint64_t last_log_ms = 0;
    uint64_t last_fail_log_ms = 0;

    ESP_LOGI(TAG, "ENV 采样任务启动，采样周期:%d ms，日志周期:%d ms",
             ENV_SAMPLE_PERIOD_MS,
             ENV_LOG_PERIOD_MS);

    while (1)
    {
        esp_err_t ret = env_try_init_sensor();
        if (ret != ESP_OK)
        {
            env_delay_ms(ENV_INIT_RETRY_DELAY_MS);
            continue;
        }

        ret = env_read_sensor_once();
        uint64_t now_ms = env_get_time_ms();

        if (ret == ESP_OK)
        {
#if ENV_LOG_ENABLE
            if (env_should_print_periodic_log(now_ms, &last_log_ms))
            {
                env_data_t latest_data = {0};
                if (env_get_data(&latest_data) == ESP_OK)
                {
                    env_print_data_log(&latest_data);
                }
            }
#endif
        }
        else
        {
            s_env_sensor_ready = false;

            if (env_should_print_fail_log(now_ms, &last_fail_log_ms))
            {
                ESP_LOGI(TAG, "ENV 读取 BME690 失败，ret: %d", ret);
            }
        }

        env_delay_ms(ENV_SAMPLE_PERIOD_MS);
    }
}

esp_err_t env_init(void)
{
    if (s_env_task_handle != NULL)
    {
        ESP_LOGI(TAG, "ENV 已初始化，采样任务已运行");
        return ESP_OK;
    }

    if (s_env_data_mutex == NULL)
    {
        s_env_data_mutex = xSemaphoreCreateMutex();
        if (s_env_data_mutex == NULL)
        {
            ESP_LOGI(TAG, "ENV 数据互斥锁创建失败");
            return ESP_ERR_NO_MEM;
        }
    }

    memset(&s_env_latest_data, 0, sizeof(s_env_latest_data));

    esp_err_t ret = env_try_init_sensor();
    if (ret != ESP_OK)
    {
        ESP_LOGI(TAG, "ENV 首次初始化 BME690 失败，仍会创建采样任务并在后台重试");
    }

    BaseType_t task_ret = xTaskCreatePinnedToCore(env_sample_task,
                                                  ENV_TASK_NAME,
                                                  ENV_TASK_STACK_SIZE,
                                                  NULL,
                                                  ENV_TASK_PRIORITY,
                                                  &s_env_task_handle,
                                                  ENV_TASK_CORE_ID);
    if (task_ret != pdPASS)
    {
        s_env_task_handle = NULL;
        ESP_LOGI(TAG, "ENV 采样任务创建失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ENV 初始化完成");
    return ESP_OK;
}

esp_err_t env_get_data(env_data_t *data)
{
    if (data == NULL)
    {
        ESP_LOGI(TAG, "env_get_data 参数错误，data 不能为 NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_env_data_mutex == NULL)
    {
        ESP_LOGI(TAG, "ENV 尚未初始化，请先调用 env_init()");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = env_lock_data();
    if (ret != ESP_OK)
    {
        return ret;
    }

    *data = s_env_latest_data;
    env_unlock_data();

#if ENV_GET_DATA_REQUIRE_VALID
    if (!data->valid)
    {
        return ESP_ERR_INVALID_STATE;
    }
#endif

    return ESP_OK;
}
