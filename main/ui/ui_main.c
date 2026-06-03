#include "ui_main.h"
#include "ui_sensor_page.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "touch_cst816t.h"

static const char *TAG = "UI_MAIN";

/* ui_page_t：UI 当前页面状态枚举。
 *
 * 功能：
 *     UI_PAGE_HOME 表示带有 Sensor 入口按键的 HOME 主界面；
 *     UI_PAGE_SENSOR 表示当前显示真实环境数据的 Sensor 页面。
 *
 * 调用方法：
 *     ui_main_switch_page(UI_PAGE_HOME);      // 返回主界面
 *     ui_main_switch_page(UI_PAGE_SENSOR);    // 进入 Sensor 页面
 */
typedef enum
{
    UI_PAGE_HOME = 0,
    UI_PAGE_SENSOR,
} ui_page_t;

static TaskHandle_t s_ui_task_handle = NULL;
static ui_page_t s_current_page = UI_PAGE_HOME;

/* ui_main_ms_to_ticks：把毫秒时间转换为 FreeRTOS tick。
 *
 * 参数：
 *     ms：需要转换的毫秒数。
 *
 * 返回：
 *     可直接传给 vTaskDelay() 的 tick 数；当 ms 很小导致转换为 0 时，至少返回 1 tick。
 *
 * 调用方法：
 *     vTaskDelay(ui_main_ms_to_ticks(UI_MAIN_POLL_PERIOD_MS));
 */
static TickType_t ui_main_ms_to_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    return ticks;
}

/* ui_main_map_touch_point：把 CST816T 原始触摸坐标映射为 LCD 像素坐标。
 *
 * 参数：
 *     raw_x：cst816t_read_point() 读到的原始 X 坐标。
 *     raw_y：cst816t_read_point() 读到的原始 Y 坐标。
 *     map_x：输出参数，保存映射后的 LCD X 坐标，不能为 NULL。
 *     map_y：输出参数，保存映射后的 LCD Y 坐标，不能为 NULL。
 *
 * 返回：
 *     true：坐标有效并完成映射；
 *     false：坐标无效或输出参数为空，本次触摸不参与 UI 判断。
 *
 * 调用方法：
 *     uint16_t x = 0;
 *     uint16_t y = 0;
 *     if (ui_main_map_touch_point(raw_x, raw_y, &x, &y)) {
 *         // 使用映射后的 x/y 判断按键区域
 *     }
 */
static bool ui_main_map_touch_point(uint16_t raw_x, uint16_t raw_y, uint16_t *map_x, uint16_t *map_y)
{
    if ((map_x == NULL) || (map_y == NULL))
    {
        return false;
    }

    if ((raw_x >= 4090U) || (raw_y >= 4090U))
    {
        return false;
    }

    int32_t scaled_x = ((int32_t)raw_x - UI_MAIN_TOUCH_X_MIN) * (int32_t)(LCD_H_RES - 1U) /
                       (UI_MAIN_TOUCH_X_MAX - UI_MAIN_TOUCH_X_MIN);
    int32_t scaled_y = ((int32_t)raw_y - UI_MAIN_TOUCH_Y_MIN) * (int32_t)(LCD_V_RES - 1U) /
                       (UI_MAIN_TOUCH_Y_MAX - UI_MAIN_TOUCH_Y_MIN);

    scaled_y = (int32_t)(LCD_V_RES - 1U) - scaled_y;

    if (scaled_x < 0)
    {
        scaled_x = 0;
    }
    else if (scaled_x >= (int32_t)LCD_H_RES)
    {
        scaled_x = (int32_t)LCD_H_RES - 1;
    }

    if (scaled_y < 0)
    {
        scaled_y = 0;
    }
    else if (scaled_y >= (int32_t)LCD_V_RES)
    {
        scaled_y = (int32_t)LCD_V_RES - 1;
    }

    *map_x = (uint16_t)scaled_x;
    *map_y = (uint16_t)scaled_y;

    return true;
}

/* ui_main_point_in_rect：判断触摸点是否落在指定矩形区域内。
 *
 * 参数：
 *     x：已经通过 ui_main_map_touch_point() 映射后的 LCD X 坐标。
 *     y：已经通过 ui_main_map_touch_point() 映射后的 LCD Y 坐标。
 *     rect_x：矩形左上角 X 坐标。
 *     rect_y：矩形左上角 Y 坐标。
 *     rect_w：矩形宽度，单位像素。
 *     rect_h：矩形高度，单位像素。
 *
 * 返回：
 *     true：触摸点位于矩形区域内；
 *     false：触摸点不在矩形区域内。
 *
 * 调用方法：
 *     if (ui_main_point_in_rect(x, y, UI_MAIN_SENSOR_BTN_X, UI_MAIN_SENSOR_BTN_Y,
 *                               UI_MAIN_SENSOR_BTN_W, UI_MAIN_SENSOR_BTN_H)) {
 *         ui_main_switch_page(UI_PAGE_SENSOR);
 *     }
 *
 * 实现说明：
 *     使用本函数统一处理 HOME 页面 Sensor 入口按键的矩形命中判断；
 *     Sensor 页面 Back 命中判断由 ui_sensor_page_is_back_hit() 完成，避免 ui_main.c 继续耦合
 *     Sensor 页面内部按钮实现。
 */
static bool ui_main_point_in_rect(uint16_t x,
                                  uint16_t y,
                                  uint16_t rect_x,
                                  uint16_t rect_y,
                                  uint16_t rect_w,
                                  uint16_t rect_h)
{
    const uint32_t point_x = x;
    const uint32_t point_y = y;
    const uint32_t left = rect_x;
    const uint32_t top = rect_y;
    const uint32_t right = left + rect_w;
    const uint32_t bottom = top + rect_h;

    return (point_x >= left) &&
           (point_x < right) &&
           (point_y >= top) &&
           (point_y < bottom);
}

/* ui_main_point_in_sensor_button：判断触摸点是否落在 HOME 页面的 Sensor 入口按键内。
 *
 * 参数：
 *     x：已经通过 ui_main_map_touch_point() 映射后的 LCD X 坐标。
 *     y：已经通过 ui_main_map_touch_point() 映射后的 LCD Y 坐标。
 *
 * 返回：
 *     true：触摸点位于 Sensor 入口按键区域内；
 *     false：触摸点不在 Sensor 入口按键区域内。
 *
 * 调用方法：
 *     if (ui_main_point_in_sensor_button(x, y)) {
 *         ui_main_switch_page(UI_PAGE_SENSOR);
 *     }
 */
static bool ui_main_point_in_sensor_button(uint16_t x, uint16_t y)
{
    return ui_main_point_in_rect(x,
                                 y,
                                 UI_MAIN_SENSOR_BTN_X,
                                 UI_MAIN_SENSOR_BTN_Y,
                                 UI_MAIN_SENSOR_BTN_W,
                                 UI_MAIN_SENSOR_BTN_H);
}

/* ui_main_draw_sensor_button：绘制 HOME 页面中的 Sensor 入口按键。
 *
 * 功能：
 *     该函数只负责在 HOME 页面绘制 Sensor 入口按钮，不读取 ENV，不读取 BME690，
 *     也不改变当前页面状态。触摸命中后的页面切换由 ui_main_handle_home_touch() 负责。
 *
 * 调用方法：
 *     ui_main_draw_screen();           // HOME 页面完整重绘时会调用本函数
 *     ui_main_draw_sensor_button();    // 需要单独刷新 Sensor 入口按键时可直接调用
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：LCD 绘图接口返回的错误码。
 */
static esp_err_t ui_main_draw_sensor_button(void)
{
    esp_err_t ret = lcd_fill_rect(UI_MAIN_SENSOR_BTN_X,
                                  UI_MAIN_SENSOR_BTN_Y,
                                  UI_MAIN_SENSOR_BTN_W,
                                  UI_MAIN_SENSOR_BTN_H,
                                  UI_MAIN_SENSOR_BTN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_draw_string(UI_MAIN_SENSOR_BTN_TEXT_X,
                           UI_MAIN_SENSOR_BTN_TEXT_Y,
                           UI_MAIN_SENSOR_BTN_TEXT,
                           UI_MAIN_BUTTON_TEXT_COLOR,
                           UI_MAIN_SENSOR_BTN_COLOR);
}

/* ui_main_draw_screen：绘制 HOME 主界面。
 *
 * 功能：
 *     1. 清屏为 UI_MAIN_SCREEN_COLOR；
 *     2. 绘制 HOME 页面标题 UI_MAIN_HOME_TITLE_TEXT；
 *     3. 绘制屏幕中间的大 Sensor 入口按键。
 *
 * 调用方法：
 *     ui_main_start();                    // 启动 UI 时绘制 HOME 页面
 *     ui_main_switch_page(UI_PAGE_HOME);  // 从 Sensor 页面返回 HOME 页面时调用
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：LCD 绘图接口返回的错误码。
 */
static esp_err_t ui_main_draw_screen(void)
{
    esp_err_t ret = lcd_clear(UI_MAIN_SCREEN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_draw_string(UI_MAIN_HOME_TITLE_X,
                          UI_MAIN_HOME_TITLE_Y,
                          UI_MAIN_HOME_TITLE_TEXT,
                          UI_MAIN_TEXT_COLOR,
                          UI_MAIN_SCREEN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* 当前 HOME 页面只保留居中的 Sensor 入口。 */
    return ui_main_draw_sensor_button();
}

/* ui_main_switch_page：切换 UI 当前页面并重绘目标页面。
 *
 * 参数：
 *     page：目标页面，支持 UI_PAGE_HOME 和 UI_PAGE_SENSOR。
 *
 * 功能：
 *     1. 更新 s_current_page 当前页面状态；
 *     2. 切换到 HOME 时重绘标题和居中 Sensor 入口按键；
 *     3. 切换到 SENSOR 时调用 ui_sensor_page_draw()，由独立 Sensor 页面模块重绘静态内容并立即刷新一次环境数据。
 *
 * 调用方法：
 *     ui_main_switch_page(UI_PAGE_HOME);      // 返回 HOME 主界面
 *     ui_main_switch_page(UI_PAGE_SENSOR);    // 进入 Sensor 数据页面
 *
 * 返回：
 *     ESP_OK：页面切换和绘制成功；
 *     ESP_ERR_INVALID_ARG：传入了未知页面；
 *     其它值：LCD 绘图接口返回的错误码。
 */
static esp_err_t ui_main_switch_page(ui_page_t page)
{
    esp_err_t ret = ESP_OK;

    switch (page)
    {
    case UI_PAGE_HOME:
        ret = ui_main_draw_screen();
        break;

    case UI_PAGE_SENSOR:
        ret = ui_sensor_page_draw();
        break;

    default:
        return ESP_ERR_INVALID_ARG;
    }

    if (ret == ESP_OK)
    {
        s_current_page = page;
    }

    return ret;
}

/* ui_main_handle_home_touch：处理 HOME 页面的一次有效触摸按下事件。
 *
 * 参数：
 *     x：已经通过 ui_main_map_touch_point() 映射后的 LCD X 坐标。
 *     y：已经通过 ui_main_map_touch_point() 映射后的 LCD Y 坐标。
 *
 * 功能：
 *     1. 只判断 HOME 页面中间的大 Sensor 按键；
 *     2. 点击 Sensor 按键时调用 ui_main_switch_page(UI_PAGE_SENSOR) 进入 Sensor 页面；
 *     3. 其它区域触摸不处理。
 *
 * 调用方法：
 *     if (s_current_page == UI_PAGE_HOME) {
 *         ui_main_handle_home_touch(x, y);
 *     }
 */
static void ui_main_handle_home_touch(uint16_t x, uint16_t y)
{
    /* 当前 HOME 页面只响应居中的 Sensor 按键，其它区域触摸直接忽略。 */
    if (!ui_main_point_in_sensor_button(x, y))
    {
        return;
    }

    esp_err_t ret = ui_main_switch_page(UI_PAGE_SENSOR);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "switch to sensor page failed: %s", esp_err_to_name(ret));
    }
}

/* ui_main_handle_sensor_touch：处理 Sensor 页面的一次有效触摸按下事件。
 *
 * 参数：
 *     x：已经通过 ui_main_map_touch_point() 映射后的 LCD X 坐标。
 *     y：已经通过 ui_main_map_touch_point() 映射后的 LCD Y 坐标。
 *
 * 功能：
 *     1. 点击左上角 Back 区域时返回 HOME 页面；
 *     2. Sensor 页面其它区域触摸不处理；
 *     3. 触摸事件里不读取 ENV，不读取 BME690，
 *        真实环境数据刷新统一由 ui_sensor_page_update() 在 UI 主循环中处理。
 *
 * 调用方法：
 *     if (s_current_page == UI_PAGE_SENSOR) {
 *         ui_main_handle_sensor_touch(x, y);
 *     }
 */
static void ui_main_handle_sensor_touch(uint16_t x, uint16_t y)
{
    if (!ui_sensor_page_is_back_hit(x, y))
    {
        return;
    }

    esp_err_t ret = ui_main_switch_page(UI_PAGE_HOME);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "switch to home page failed: %s", esp_err_to_name(ret));
    }
}

/* ui_main_dispatch_touch：根据当前页面分发一次有效触摸按下事件。
 *
 * 参数：
 *     x：已经通过 ui_main_map_touch_point() 映射后的 LCD X 坐标。
 *     y：已经通过 ui_main_map_touch_point() 映射后的 LCD Y 坐标。
 *
 * 功能：
 *     HOME 页面只处理居中 Sensor 按键；
 *     SENSOR 页面只处理 Back；
 *     未知页面不处理，并打印错误日志方便调试。
 *
 * 调用方法：
 *     ui_main_dispatch_touch(x, y);
 */
static void ui_main_dispatch_touch(uint16_t x, uint16_t y)
{
    switch (s_current_page)
    {
    case UI_PAGE_HOME:
        ui_main_handle_home_touch(x, y);
        break;

    case UI_PAGE_SENSOR:
        ui_main_handle_sensor_touch(x, y);
        break;

    default:
        ESP_LOGE(TAG, "unknown ui page: %d", (int)s_current_page);
        break;
    }
}

/* ui_main_task：UI 触摸轮询和事件分发任务。
 *
 * 参数：
 *     arg：任务参数，当前未使用，保留给后续扩展。
 *
 * 功能：
 *     1. 周期调用 cst816t_read_point() 读取触摸状态；
 *     2. 只在新的按下沿处理一次触摸，避免长按时反复切换页面；
 *     3. 根据 s_current_page 分发 HOME/SENSOR 页面事件；
 *     4. 当当前页面是 UI_PAGE_SENSOR 时，周期调用 ui_sensor_page_update(false)，
 *        按 UI_MAIN_SENSOR_REFRESH_MS 间隔局部刷新环境数据文本。
 *
 * 调用方法：
 *     ui_main_start();    // 内部通过 xTaskCreate() 创建本任务，不建议外部直接调用
 */
static void ui_main_task(void *arg)
{
    (void)arg;

    bool touch_active = false;
    while (1)
    {
        uint16_t raw_x = 0;
        uint16_t raw_y = 0;
        bool pressed = false;

        bool read_ok = cst816t_read_point(&raw_x, &raw_y, &pressed);
        if (read_ok && pressed)
        {
            uint16_t x = 0;
            uint16_t y = 0;

            if (!touch_active)
            {
                touch_active = true;

                if (ui_main_map_touch_point(raw_x, raw_y, &x, &y))
                {
                    ui_main_dispatch_touch(x, y);
                }
            }
        }
        else if (read_ok)
        {
            touch_active = false;
        }

        if (s_current_page == UI_PAGE_SENSOR)
        {
            esp_err_t ret = ui_sensor_page_update(false);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "sensor data refresh failed: %s", esp_err_to_name(ret));
            }
        }

        vTaskDelay(ui_main_ms_to_ticks(UI_MAIN_POLL_PERIOD_MS));
    }
}

/* ui_main_start：启动 HOME/Sensor 页面切换 UI。
 *
 * 功能：
 *     1. 初始化 UI 内部状态；
 *     2. 绘制 HOME 页面标题和居中 Sensor 入口；
 *     3. 创建 UI 触摸轮询任务，处理 HOME 的 Sensor 入口和 Sensor 页面的 Back 返回。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_init());
 *     ESP_ERROR_CHECK(cst816t_init());
 *     ESP_ERROR_CHECK(ui_main_start());
 *
 * 返回：
 *     ESP_OK：启动成功，或 UI 任务已经运行；
 *     ESP_ERR_NO_MEM：FreeRTOS 任务创建失败；
 *     其它值：LCD 绘图接口返回的错误码。
 */
esp_err_t ui_main_start(void)
{
    if (s_ui_task_handle != NULL)
    {
        ESP_LOGI(TAG, "UI task already running");
        return ESP_OK;
    }

    s_current_page = UI_PAGE_HOME;

    esp_err_t ret = ui_main_switch_page(UI_PAGE_HOME);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "UI draw failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(ui_main_task,
                                      UI_MAIN_TASK_NAME,
                                      UI_MAIN_TASK_STACK_SIZE,
                                      NULL,
                                      UI_MAIN_TASK_PRIORITY,
                                      &s_ui_task_handle);
    if (task_ret != pdPASS)
    {
        s_ui_task_handle = NULL;
        ESP_LOGE(TAG, "UI task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UI started");
    return ESP_OK;
}
