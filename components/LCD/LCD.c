#include "LCD.h"

#include <string.h>

#include "esp_log.h"

#if LCD_ENABLE_DASHBOARD_UI
#include "lcd_ui.h"
#endif

static const char *TAG = "LCD";

typedef struct _lcd_state_t
{
    lcd_config_t config;             /* LCD 顶层配置，包含 Driver 配置和数据回调 */
    TaskHandle_t task_handle;        /* LCD 自动刷新任务句柄，用于避免重复创建任务 */
    bool initialized;                /* LCD 顶层模块是否已经初始化完成 */
} lcd_state_t;

static lcd_state_t s_lcd_state;      /* LCD 顶层私有状态，保持模块内部自洽 */

#if LCD_ENABLE_DASHBOARD_UI

/* lcd_delay_ms：LCD 顶层任务延时函数。
 *
 * 参数：
 *     delay_ms：需要延时的毫秒数。
 *
 * 调用方法：
 *     lcd_delay_ms(LCD_REFRESH_PERIOD_MS);
 */
static void lcd_delay_ms(uint32_t delay_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);

    if (ticks == 0)
    {
        ticks = LCD_WAIT_TICK_MIN;
    }

    vTaskDelay(ticks);
}
#endif

#if LCD_ENABLE_DASHBOARD_UI

/* lcd_fill_default_dashboard_data：生成默认 Dashboard 数据。
 *
 * 参数：
 *     data：输出数据指针，不能为 NULL。
 *
 * 调用方法：
 *     lcd_dashboard_data_t data = {0};
 *     lcd_fill_default_dashboard_data(&data);
 *
 * 说明：
 *     当业务层尚未提供 ENV/WiFi/ASR 状态时，LCD 仍然可以稳定显示占位页面。
 */
static void lcd_fill_default_dashboard_data(lcd_dashboard_data_t *data)
{
    if (data == NULL)
    {
        return;
    }

    data->temperature_c = LCD_DATA_DEFAULT_VALUE;
    data->humidity_percent = LCD_DATA_DEFAULT_VALUE;
    data->pressure_hpa = LCD_DATA_DEFAULT_VALUE;
    data->gas_resistance_ohm = 0;
    data->env_valid = false;
    data->wifi_connected = false;
    data->asr_active = false;
}

/* lcd_get_dashboard_data：获取当前 Dashboard 数据快照。
 *
 * 参数：
 *     data：输出数据指针，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：获取成功；
 *     其它值：业务数据回调返回的错误。
 *
 * 调用方法：
 *     lcd_dashboard_data_t data = {0};
 *     lcd_get_dashboard_data(&data);
 */
static esp_err_t lcd_get_dashboard_data(lcd_dashboard_data_t *data)
{
    if (data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    lcd_fill_default_dashboard_data(data);

    if (s_lcd_state.config.data_cb == NULL)
    {
        return ESP_OK;
    }

    esp_err_t ret = s_lcd_state.config.data_cb(data, s_lcd_state.config.user_ctx);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Dashboard 数据回调失败，ret: %d，使用默认占位数据", ret);
        lcd_fill_default_dashboard_data(data);
    }

    return ESP_OK;
}

/* lcd_convert_to_ui_data：把 LCD 顶层数据结构转换成 UI 数据结构。
 *
 * 参数：
 *     src：LCD 顶层 Dashboard 数据，不能为 NULL。
 *     dst：UI Dashboard 数据，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：转换成功；
 *     ESP_ERR_INVALID_ARG：参数为空。
 *
 * 调用方法：
 *     lcd_convert_to_ui_data(&dashboard_data, &ui_data);
 */
static esp_err_t lcd_convert_to_ui_data(const lcd_dashboard_data_t *src, lcd_ui_dashboard_data_t *dst)
{
    if ((src == NULL) || (dst == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    dst->temperature_c = src->temperature_c;
    dst->humidity_percent = src->humidity_percent;
    dst->pressure_hpa = src->pressure_hpa;
    dst->gas_resistance_ohm = src->gas_resistance_ohm;
    dst->env_valid = src->env_valid;
    dst->wifi_connected = src->wifi_connected;
    dst->asr_active = src->asr_active;

    return ESP_OK;
}
#endif

#if LCD_ENABLE_DASHBOARD_UI

/* lcd_task：LCD Dashboard 自动刷新任务。
 *
 * 参数：
 *     arg：任务参数，当前未使用，保留用于后续扩展。
 *
 * 功能：
 *     每隔 LCD_REFRESH_PERIOD_MS 调用 lcd_refresh_once() 刷新一次页面。
 *
 * 调用方法：
 *     该函数由 lcd_init() 内部创建，不建议外部直接调用。
 */
static void lcd_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "LCD 刷新任务启动，刷新周期: %d ms", LCD_REFRESH_PERIOD_MS);

    while (1)
    {
        esp_err_t ret = lcd_refresh_once();
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "LCD 刷新失败，ret: %d", ret);
        }

        lcd_delay_ms(LCD_REFRESH_PERIOD_MS);
    }
}
#endif

esp_err_t lcd_get_default_config(lcd_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));

    esp_err_t ret = lcd_st7789_get_default_config(&config->driver_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    config->data_cb = NULL;
    config->user_ctx = NULL;
    config->start_task = LCD_START_TASK_DEFAULT;

    return ESP_OK;
}

esp_err_t lcd_init(const lcd_config_t *config)
{
    if (s_lcd_state.initialized)
    {
        ESP_LOGI(TAG, "LCD 模块已初始化");
        return ESP_OK;
    }

    lcd_config_t local_config = {0};
    esp_err_t ret;

    if (config == NULL)
    {
        ret = lcd_get_default_config(&local_config);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    else
    {
        local_config = *config;
    }

    memset(&s_lcd_state, 0, sizeof(s_lcd_state));
    s_lcd_state.config = local_config;

    ret = lcd_st7789_init(&s_lcd_state.config.driver_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ST7789 Driver 初始化失败，ret: %d", ret);
        return ret;
    }

    s_lcd_state.initialized = true;

    ret = lcd_fill_screen_test();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD 纯色填充测试失败，ret: %d", ret);
        return ret;
    }

#if LCD_ENABLE_DASHBOARD_UI
    if (s_lcd_state.config.start_task)
    {
        BaseType_t task_ret = xTaskCreatePinnedToCore(lcd_task,
                                                       LCD_TASK_NAME,
                                                       LCD_TASK_STACK_SIZE,
                                                       NULL,
                                                       LCD_TASK_PRIORITY,
                                                       &s_lcd_state.task_handle,
                                                       LCD_TASK_CORE_ID);
        if (task_ret != pdPASS)
        {
            s_lcd_state.task_handle = NULL;
            ESP_LOGE(TAG, "LCD 刷新任务创建失败");
            return ESP_ERR_NO_MEM;
        }
    }
#else
    if (s_lcd_state.config.start_task)
    {
        ESP_LOGW(TAG, "当前为纯色 fill_screen 测试阶段，已忽略 start_task，未创建 LCD 刷新任务");
    }
#endif

    ESP_LOGI(TAG, "LCD 模块初始化完成，已显示纯色测试画面，未启用 LVGL/复杂 UI/多任务刷新");
    return ESP_OK;
}

esp_err_t lcd_fill_screen_test(void)
{
    if (!s_lcd_state.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = lcd_st7789_fill_screen(LCD_FILL_TEST_COLOR);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD fill_screen 失败，color: 0x%04X, ret: %d", LCD_FILL_TEST_COLOR, ret);
        return ret;
    }

    ret = lcd_st7789_flush();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD flush 失败，ret: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "LCD 纯色 fill_screen 测试完成，color: 0x%04X", LCD_FILL_TEST_COLOR);
    return ESP_OK;
}

esp_err_t lcd_refresh_once(void)
{
    if (!s_lcd_state.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

#if LCD_ENABLE_DASHBOARD_UI
    lcd_dashboard_data_t dashboard_data = {0};
    esp_err_t ret = lcd_get_dashboard_data(&dashboard_data);
    if (ret != ESP_OK)
    {
        return ret;
    }

    lcd_ui_dashboard_data_t ui_data = {0};
    ret = lcd_convert_to_ui_data(&dashboard_data, &ui_data);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_ui_draw_dashboard(&ui_data);
#else
    return lcd_fill_screen_test();
#endif
}

esp_err_t lcd_set_dashboard_data_callback(lcd_dashboard_data_cb_t data_cb, void *user_ctx)
{
    if (!s_lcd_state.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_lcd_state.config.data_cb = data_cb;
    s_lcd_state.config.user_ctx = user_ctx;

    return ESP_OK;
}

bool lcd_is_initialized(void)
{
    return s_lcd_state.initialized;
}
