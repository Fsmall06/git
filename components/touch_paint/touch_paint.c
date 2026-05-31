#include "touch_paint.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_cst816t.h"

static const char *TAG = TOUCH_PAINT_LOG_TAG;

/* s_touch_paint_task_handle：触摸画板任务句柄。
 *
 * 说明：
 *     用于判断 touch_paint_start() 是否已经创建过任务，避免重复创建多个任务同时读触摸和写 LCD。
 */
static TaskHandle_t s_touch_paint_task_handle = NULL;

/* touch_paint_ms_to_ticks：把毫秒转换为 FreeRTOS tick。
 *
 * 参数：
 *     ms：需要转换的毫秒数。
 *
 * 返回：
 *     可直接传给 vTaskDelay() 的 tick 数；当 ms 很小导致转换为 0 时，至少返回 1 tick。
 *
 * 调用方法：
 *     vTaskDelay(touch_paint_ms_to_ticks(TOUCH_PAINT_PERIOD_MS));
 */
static TickType_t touch_paint_ms_to_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    return ticks;
}

/* touch_paint_should_print_log：判断限流日志是否到达打印时间。
 *
 * 参数：
 *     last_log_tick：上一次打印日志时的系统 tick 指针，不能为 NULL。
 *     period_ms：日志限流周期，单位 ms。
 *
 * 返回：
 *     true：距离上次打印已经超过 period_ms，本次允许打印；
 *     false：距离上次打印太近，本次不打印。
 *
 * 调用方法：
 *     if (touch_paint_should_print_log(&last_log_tick, TOUCH_PAINT_LOG_PERIOD_MS)) {
 *         ESP_LOGI(TAG, "TOUCH:\nx=%u\ny=%u", x, y);
 *     }
 */
static bool touch_paint_should_print_log(TickType_t *last_log_tick, uint32_t period_ms)
{
    if (last_log_tick == NULL)
    {
        return false;
    }

    const TickType_t now_tick = xTaskGetTickCount();
    const TickType_t period_tick = touch_paint_ms_to_ticks(period_ms);

    if ((now_tick - *last_log_tick) < period_tick)
    {
        return false;
    }

    *last_log_tick = now_tick;
    return true;
}

/* touch_paint_is_drawable_point：判断触摸坐标是否适合绘制 5x5 实心点。
 *
 * 参数：
 *     x：触摸 X 坐标。
 *     y：触摸 Y 坐标。
 *
 * 返回：
 *     true：以该坐标为中心绘制 TOUCH_PAINT_POINT_SIZE 方点不会越界；
 *     false：坐标太靠近边缘或超出屏幕，本次跳过绘制。
 *
 * 边界保护：
 *     1. 避免 x < 2；
 *     2. 避免 y < 2；
 *     3. 避免 x + 2 >= LCD_H_RES；
 *     4. 避免 y + 2 >= LCD_V_RES。
 */
static bool touch_paint_is_valid_point(uint16_t x, uint16_t y)
{
    if ((x >= LCD_H_RES) || (y >= LCD_V_RES))
    {
        return false;
    }

    return true;
}

static bool touch_paint_map_point(uint16_t raw_x, uint16_t raw_y, uint16_t *map_x, uint16_t *map_y)
{
    if ((map_x == NULL) || (map_y == NULL))
    {
        return false;
    }

    *map_x = raw_x;
    *map_y = raw_y;

    if (!touch_paint_is_valid_point(raw_x, raw_y))
    {
        return false;
    }

    *map_x = raw_x;
    *map_y = (uint16_t)(LCD_V_RES - 1U - raw_y);

    return true;
}

/* touch_paint_draw_point：以触摸坐标为中心绘制红色实心点。
 *
 * 参数：
 *     x：触摸 X 坐标。
 *     y：触摸 Y 坐标。
 *
 * 返回：
 *     ESP_OK：绘制成功，或坐标位于边界保护区被安全跳过；
 *     其它值：LCD 绘图接口返回的错误码。
 *
 * 调用方法：
 *     touch_paint_draw_point(x, y);
 */
static esp_err_t touch_paint_draw_point(uint16_t x, uint16_t y)
{
    if (!touch_paint_is_valid_point(x, y))
    {
        return ESP_OK;
    }

    const uint16_t radius = TOUCH_PAINT_POINT_RADIUS;
    const uint16_t x0 = (x > radius) ? (uint16_t)(x - radius) : 0U;
    const uint16_t y0 = (y > radius) ? (uint16_t)(y - radius) : 0U;
    const uint32_t x1_candidate = (uint32_t)x + radius + 1U;
    const uint32_t y1_candidate = (uint32_t)y + radius + 1U;
    const uint16_t x1 = (x1_candidate > LCD_H_RES) ? LCD_H_RES : (uint16_t)x1_candidate;
    const uint16_t y1 = (y1_candidate > LCD_V_RES) ? LCD_V_RES : (uint16_t)y1_candidate;

    return lcd_fill_rect(x0,
                         y0,
                         (uint16_t)(x1 - x0),
                         (uint16_t)(y1 - y0),
                         TOUCH_PAINT_POINT_COLOR);
}

/* touch_paint_draw_line：在两个触摸点之间补点，形成连续手指轨迹。
 *
 * 参数：
 *     x0/y0：上一帧有效触摸坐标。
 *     x1/y1：当前帧有效触摸坐标。
 *
 * 返回：
 *     ESP_OK：线段绘制完成；
 *     其它值：某一次 lcd_fill_rect() 失败时返回对应错误码。
 *
 * 实现说明：
 *     使用整数 Bresenham 算法沿线段补点，每个补点仍然绘制 5x5 红色实心点。
 *     这样即使手指移动速度稍快，也能减少相邻采样点之间的空隙。
 */
static esp_err_t touch_paint_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    int32_t current_x = x0;
    int32_t current_y = y0;
    const int32_t target_x = x1;
    const int32_t target_y = y1;

    const int32_t dx = (current_x > target_x) ? (current_x - target_x) : (target_x - current_x);
    const int32_t dy = (current_y > target_y) ? (current_y - target_y) : (target_y - current_y);
    const int32_t step_x = (current_x < target_x) ? 1 : -1;
    const int32_t step_y = (current_y < target_y) ? 1 : -1;
    int32_t error = dx - dy;

    while (1)
    {
        esp_err_t ret = touch_paint_draw_point((uint16_t)current_x, (uint16_t)current_y);
        if (ret != ESP_OK)
        {
            return ret;
        }

        if ((current_x == target_x) && (current_y == target_y))
        {
            break;
        }

        const int32_t error2 = error * 2;

        if (error2 > -dy)
        {
            error -= dy;
            current_x += step_x;
        }

        if (error2 < dx)
        {
            error += dx;
            current_y += step_y;
        }
    }

    return ESP_OK;
}

/* touch_paint_prepare_screen：初始化画板显示内容。
 *
 * 功能：
 *     1. 清屏为黑色；
 *     2. 在顶部绘制白色 "Touch Paint Demo" 标题。
 *
 * 返回：
 *     ESP_OK：画板初始界面绘制成功；
 *     其它值：LCD 清屏或字符串绘制失败。
 *
 * 调用方法：
 *     touch_paint_start() 内部调用，业务代码通常不需要直接调用。
 */
static esp_err_t touch_paint_prepare_screen(void)
{
    esp_err_t ret = lcd_clear(TOUCH_PAINT_BACKGROUND_COLOR);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD clear failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = lcd_draw_string(TOUCH_PAINT_TITLE_X,
                          TOUCH_PAINT_TITLE_Y,
                          TOUCH_PAINT_TITLE_TEXT,
                          TOUCH_PAINT_TITLE_COLOR,
                          TOUCH_PAINT_TITLE_BACKGROUND_COLOR);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD title draw failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t touch_paint_start(void)
{
    if (s_touch_paint_task_handle != NULL)
    {
        ESP_LOGI(TAG, "Touch Paint task already running");
        return ESP_OK;
    }

    esp_err_t ret = touch_paint_prepare_screen();
    if (ret != ESP_OK)
    {
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(touch_paint_task,
                                      TOUCH_PAINT_TASK_NAME,
                                      TOUCH_PAINT_TASK_STACK_SIZE,
                                      NULL,
                                      TOUCH_PAINT_TASK_PRIORITY,
                                      &s_touch_paint_task_handle);
    if (task_ret != pdPASS)
    {
        s_touch_paint_task_handle = NULL;
        ESP_LOGE(TAG, "Touch Paint task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Touch Paint started, period=%u ms", (unsigned int)TOUCH_PAINT_PERIOD_MS);
    return ESP_OK;
}

void touch_paint_task(void *arg)
{
    (void)arg;

    uint16_t last_x = 0;
    uint16_t last_y = 0;
    bool has_last_point = false;
    TickType_t last_touch_log_tick = 0;
    TickType_t last_error_log_tick = 0;

    while (1)
    {
        uint16_t x = 0;
        uint16_t y = 0;
        bool pressed = false;

        bool read_ok = cst816t_read_point(&x, &y, &pressed);
        if (read_ok && pressed)
        {
            uint16_t map_x = 0;
            uint16_t map_y = 0;
            bool point_valid = touch_paint_map_point(x, y, &map_x, &map_y);

            if (touch_paint_should_print_log(&last_touch_log_tick, TOUCH_PAINT_LOG_PERIOD_MS))
            {
                ESP_LOGI(TAG,
                         "RAW:\nx=%u\ny=%u\nMAP:\nx=%u\ny=%u",
                         (unsigned int)x,
                         (unsigned int)y,
                         (unsigned int)map_x,
                         (unsigned int)map_y);
            }

            if (point_valid)
            {
                esp_err_t ret = ESP_OK;

                if (has_last_point && touch_paint_is_valid_point(last_x, last_y))
                {
                    ret = touch_paint_draw_line(last_x, last_y, map_x, map_y);
                }
                else
                {
                    ret = touch_paint_draw_point(map_x, map_y);
                }

                if ((ret != ESP_OK) &&
                    touch_paint_should_print_log(&last_error_log_tick, TOUCH_PAINT_LOG_PERIOD_MS))
                {
                    ESP_LOGE(TAG, "Touch paint draw failed: %s", esp_err_to_name(ret));
                }

                last_x = map_x;
                last_y = map_y;
                has_last_point = true;
            }
            else
            {
                has_last_point = false;
            }
        }
        else if (read_ok)
        {
            has_last_point = false;
        }

        vTaskDelay(touch_paint_ms_to_ticks(TOUCH_PAINT_PERIOD_MS));
    }
}
