#include "lvgl_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd.h"
#include "lvgl.h"
#include "touch_cst816t.h"

static const char *TAG = LVGL_PORT_LOG_TAG;

/* LVGL 适配层内部状态。
 *
 * 说明：
 *     这些变量只在 lvgl_port.c 内部使用，外部业务只通过 lvgl_port.h 访问 LVGL 适配层。
 */
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_disp_drv_t s_lvgl_disp_drv;
static lv_indev_drv_t s_lvgl_indev_drv;
static lv_color_t *s_lvgl_buf1 = NULL;
static lv_disp_t *s_lvgl_disp = NULL;
static lv_indev_t *s_lvgl_indev = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;
static bool s_lvgl_initialized = false;

/* lvgl_port_ms_to_ticks：把毫秒转换为 FreeRTOS tick。
 *
 * 参数：
 *     ms：毫秒数。
 *
 * 返回：
 *     可直接传给 vTaskDelay()/xSemaphoreTake() 的 tick 数；当 ms 很小时至少返回 1 tick。
 *
 * 调用方法：
 *     vTaskDelay(lvgl_port_ms_to_ticks(LVGL_PORT_TASK_PERIOD_MS));
 */
static TickType_t lvgl_port_ms_to_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    return ticks;
}

/* lvgl_port_color_to_rgb565：把 LVGL 颜色转换为现有 LCD 接口使用的标准 RGB565。
 *
 * 参数：
 *     color：LVGL 渲染缓冲区中的颜色。
 *
 * 返回：
 *     标准 RGB565 颜色值，例如红色为 0xF800。
 *
 * 说明：
 *     lcd_fill_rect() 内部已经处理 LCD 面板需要的字节序转换，这里只输出标准 RGB565，
 *     避免重复处理 LCD 驱动里已经验证通过的颜色逻辑。
 */
static uint16_t lvgl_port_color_to_rgb565(lv_color_t color)
{
    return lv_color_to16(color);
}

/* lvgl_port_map_touch_point：复用已验证的 touch_paint 坐标映射公式。
 *
 * 参数：
 *     raw_x/raw_y：cst816t_read_point() 输出的原始坐标；
 *     map_x/map_y：映射后的 LCD 像素坐标输出指针，不能为 NULL。
 *
 * 返回：
 *     true：映射成功；
 *     false：参数错误或原始坐标无效。
 *
 * 重要：
 *     这里没有修改 touch_paint 模块，也没有修改 CST816T 驱动源码；
 *     只是把已验证通过的 raw -> LCD 坐标公式原样放在 LVGL 适配层中使用。
 */
static bool lvgl_port_map_touch_point(uint16_t raw_x, uint16_t raw_y, uint16_t *map_x, uint16_t *map_y)
{
    if ((map_x == NULL) || (map_y == NULL))
    {
        return false;
    }

    if ((raw_x >= LVGL_PORT_TOUCH_RAW_INVALID_LIMIT) || (raw_y >= LVGL_PORT_TOUCH_RAW_INVALID_LIMIT))
    {
        return false;
    }

    int32_t scaled_x = ((int32_t)raw_x - (int32_t)LVGL_PORT_TOUCH_RAW_X_MIN) *
                       (int32_t)(LCD_H_RES - 1U) /
                       ((int32_t)LVGL_PORT_TOUCH_RAW_X_MAX - (int32_t)LVGL_PORT_TOUCH_RAW_X_MIN);
    int32_t scaled_y = ((int32_t)raw_y - (int32_t)LVGL_PORT_TOUCH_RAW_Y_MIN) *
                       (int32_t)(LCD_V_RES - 1U) /
                       ((int32_t)LVGL_PORT_TOUCH_RAW_Y_MAX - (int32_t)LVGL_PORT_TOUCH_RAW_Y_MIN);

#if LVGL_PORT_TOUCH_INVERT_Y
    scaled_y = (int32_t)(LCD_V_RES - 1U) - scaled_y;
#endif

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

/* lvgl_port_tick_cb：LVGL tick 定时器回调。
 *
 * 参数：
 *     arg：esp_timer 用户参数，本项目未使用。
 *
 * 功能：
 *     周期调用 lv_tick_inc()，为 LVGL 提供毫秒时间基准。
 *
 * 调用方法：
 *     lvgl_port_init();   // 内部创建 esp_timer 并自动调用本回调
 */
static void lvgl_port_tick_cb(void *arg)
{
    (void)arg;

    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

/* lvgl_flush_cb：LVGL 显示刷新回调。
 *
 * 参数：
 *     drv：LVGL 显示驱动对象；
 *     area：LVGL 要刷新的矩形区域，x1/y1/x2/y2 为包含式坐标；
 *     color_p：LVGL 渲染好的像素颜色缓冲区。
 *
 * 功能：
 *     使用现有 lcd_fill_rect() 把 LVGL 像素写入屏幕，不修改 LCD 驱动源码。
 *
 * 实现说明：
 *     当前 LCD 对外接口没有公开 bitmap 刷屏函数，因此这里按行扫描相同颜色的连续像素段，
 *     再调用 lcd_fill_rect() 刷新。这个方式对最小按钮页面足够稳定，也能最大限度保护
 *     已验证通过的 LCD 初始化和 SPI 逻辑。
 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    if ((drv == NULL) || (area == NULL) || (color_p == NULL))
    {
        if (drv != NULL)
        {
            lv_disp_flush_ready(drv);
        }
        return;
    }

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if ((x2 < 0) || (y2 < 0) || (x1 >= LCD_H_RES) || (y1 >= LCD_V_RES))
    {
        lv_disp_flush_ready(drv);
        return;
    }

    if (x1 < 0)
    {
        x1 = 0;
    }

    if (y1 < 0)
    {
        y1 = 0;
    }

    if (x2 >= LCD_H_RES)
    {
        x2 = LCD_H_RES - 1;
    }

    if (y2 >= LCD_V_RES)
    {
        y2 = LCD_V_RES - 1;
    }

    const int32_t area_width = (int32_t)area->x2 - (int32_t)area->x1 + 1;

    for (int32_t y = y1; y <= y2; y++)
    {
        int32_t x = x1;

        while (x <= x2)
        {
            const int32_t pixel_index = ((y - (int32_t)area->y1) * area_width) + (x - (int32_t)area->x1);
            const uint16_t color = lvgl_port_color_to_rgb565(color_p[pixel_index]);
            int32_t run_end = x;

            while (run_end < x2)
            {
                const int32_t next_index = ((y - (int32_t)area->y1) * area_width) +
                                           ((run_end + 1) - (int32_t)area->x1);
                const uint16_t next_color = lvgl_port_color_to_rgb565(color_p[next_index]);

                if (next_color != color)
                {
                    break;
                }

                run_end++;
            }

            esp_err_t ret = lcd_fill_rect((uint16_t)x,
                                          (uint16_t)y,
                                          (uint16_t)(run_end - x + 1),
                                          1U,
                                          color);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "LVGL flush failed: %s", esp_err_to_name(ret));
                lv_disp_flush_ready(drv);
                return;
            }

            x = run_end + 1;
        }
    }

    lv_disp_flush_ready(drv);
}

/* lvgl_touch_read_cb：LVGL 触摸输入读取回调。
 *
 * 参数：
 *     drv：LVGL 输入驱动对象；
 *     data：LVGL 输入数据输出对象。
 *
 * 功能：
 *     1. 调用现有 cst816t_read_point() 读取 CST816T 原始坐标；
 *     2. 使用已验证 touch_paint 映射公式转换为 LCD 坐标；
 *     3. 输出给 LVGL，用于按钮点击等触摸事件。
 *
 * 调用方法：
 *     lvgl_port_init();   // 内部注册本回调，LVGL 会自动周期调用
 */
static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    if (data == NULL)
    {
        return;
    }

    static uint16_t last_x = 0;
    static uint16_t last_y = 0;

    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    uint16_t map_x = 0;
    uint16_t map_y = 0;
    bool pressed = false;

    bool read_ok = cst816t_read_point(&raw_x, &raw_y, &pressed);
    if (read_ok && pressed && lvgl_port_map_touch_point(raw_x, raw_y, &map_x, &map_y))
    {
        last_x = map_x;
        last_y = map_y;
        data->point.x = (lv_coord_t)map_x;
        data->point.y = (lv_coord_t)map_y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }

    data->point.x = (lv_coord_t)last_x;
    data->point.y = (lv_coord_t)last_y;
    data->state = LV_INDEV_STATE_REL;
}

/* lvgl_port_task：LVGL 周期处理任务。
 *
 * 参数：
 *     arg：任务参数，本项目未使用。
 *
 * 功能：
 *     周期调用 lv_timer_handler()，让 LVGL 执行刷新、输入读取和事件回调。
 *
 * 调用方法：
 *     lvgl_port_start();  // 内部创建本任务
 */
static void lvgl_port_task(void *arg)
{
    (void)arg;

    while (1)
    {
        uint32_t delay_ms = LVGL_PORT_TASK_PERIOD_MS;

        if (lvgl_port_lock(LVGL_PORT_LOCK_TIMEOUT_MS))
        {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }

        if (delay_ms < LVGL_PORT_TASK_PERIOD_MS)
        {
            delay_ms = LVGL_PORT_TASK_PERIOD_MS;
        }
        else if (delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS)
        {
            delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        }

        vTaskDelay(lvgl_port_ms_to_ticks(delay_ms));
    }
}

esp_err_t lvgl_port_init(void)
{
    if (s_lvgl_initialized)
    {
        ESP_LOGI(TAG, "LVGL port already initialized");
        return ESP_OK;
    }

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == NULL)
    {
        ESP_LOGE(TAG, "LVGL mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    const size_t buf_pixels = (size_t)LCD_H_RES * (size_t)LVGL_PORT_DISP_BUFFER_LINES;
    s_lvgl_buf1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_lvgl_buf1 == NULL)
    {
        ESP_LOGE(TAG, "LVGL draw buffer malloc failed, pixels=%u", (unsigned int)buf_pixels);
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_lvgl_draw_buf, s_lvgl_buf1, NULL, (uint32_t)buf_pixels);

    lv_disp_drv_init(&s_lvgl_disp_drv);
    s_lvgl_disp_drv.hor_res = LCD_H_RES;
    s_lvgl_disp_drv.ver_res = LCD_V_RES;
    s_lvgl_disp_drv.flush_cb = lvgl_flush_cb;
    s_lvgl_disp_drv.draw_buf = &s_lvgl_draw_buf;
    s_lvgl_disp = lv_disp_drv_register(&s_lvgl_disp_drv);
    if (s_lvgl_disp == NULL)
    {
        ESP_LOGE(TAG, "LVGL display driver register failed");
        return ESP_ERR_NO_MEM;
    }

    lv_indev_drv_init(&s_lvgl_indev_drv);
    s_lvgl_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_lvgl_indev_drv.read_cb = lvgl_touch_read_cb;
    s_lvgl_indev = lv_indev_drv_register(&s_lvgl_indev_drv);
    if (s_lvgl_indev == NULL)
    {
        ESP_LOGE(TAG, "LVGL input driver register failed");
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_port_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };

    esp_err_t ret = esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LVGL tick timer create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_periodic(s_lvgl_tick_timer, (uint64_t)LVGL_PORT_TICK_PERIOD_MS * 1000ULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LVGL tick timer start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_lvgl_initialized = true;

    ESP_LOGI(TAG,
             "LVGL port initialized, res=%dx%d, buf_lines=%u, tick=%u ms",
             LCD_H_RES,
             LCD_V_RES,
             (unsigned int)LVGL_PORT_DISP_BUFFER_LINES,
             (unsigned int)LVGL_PORT_TICK_PERIOD_MS);

    return ESP_OK;
}

esp_err_t lvgl_port_start(void)
{
    if (!s_lvgl_initialized)
    {
        ESP_LOGE(TAG, "LVGL port not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_lvgl_task_handle != NULL)
    {
        ESP_LOGI(TAG, "LVGL task already running");
        return ESP_OK;
    }

    BaseType_t task_ret = xTaskCreate(lvgl_port_task,
                                      LVGL_PORT_TASK_NAME,
                                      LVGL_PORT_TASK_STACK_SIZE,
                                      NULL,
                                      LVGL_PORT_TASK_PRIORITY,
                                      &s_lvgl_task_handle);
    if (task_ret != pdPASS)
    {
        s_lvgl_task_handle = NULL;
        ESP_LOGE(TAG, "LVGL task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL task started");
    return ESP_OK;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    if (s_lvgl_mutex == NULL)
    {
        return false;
    }

    return xSemaphoreTakeRecursive(s_lvgl_mutex, lvgl_port_ms_to_ticks(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    if (s_lvgl_mutex == NULL)
    {
        return;
    }

    xSemaphoreGiveRecursive(s_lvgl_mutex);
}
