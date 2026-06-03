#include "ui_sensor_page.h"

#include "ui_main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "env.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"

typedef struct _ui_sensor_page_font5x7_t
{
    char ch;                                  /* ASCII 字符。 */
    uint8_t column[UI_MAIN_SMALL_FONT_WIDTH]; /* 小号字体点阵列数据，bit0 表示顶部像素，最高有效行由 UI_MAIN_SMALL_FONT_HEIGHT 控制。 */
} ui_sensor_page_font5x7_t;

/* Sensor 数据区专用 5x7 小号 ASCII 字库。
 *
 * 功能：
 *     1. 只覆盖 Sensor 数据页面会用到的字符，避免为了显示几行环境数据去扩展 LCD 底层 API；
 *     2. 配合 UI_MAIN_SENSOR_DATA_FONT_SCALE_X/Y 宏，可以在 240 像素宽屏幕内完整显示
 *        "Pressure: xx.xx hPa" 这类较长数据行；
 *     3. 该字库只服务 Sensor 数据文本绘制，不影响 HOME 页面标题、Sensor 标题和按键文字。
 *
 * 调用方法：
 *     ui_sensor_page_draw_small_string(UI_MAIN_SENSOR_DATA_X,
 *                                      UI_MAIN_SENSOR_DATA_Y,
 *                                      UI_MAIN_SENSOR_TEMP_FMT 格式化后的字符串,
 *                                      UI_MAIN_SENSOR_DATA_TEXT_COLOR);
 */
static const ui_sensor_page_font5x7_t s_ui_sensor_page_font5x7[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {UI_MAIN_SMALL_FONT_FALLBACK_CHAR, {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'a', {0x20, 0x54, 0x54, 0x54, 0x78}},
    {'d', {0x38, 0x44, 0x44, 0x48, 0x7F}},
    {'e', {0x38, 0x54, 0x54, 0x54, 0x18}},
    {'f', {0x08, 0x7E, 0x09, 0x01, 0x02}},
    {'g', {0x0C, 0x52, 0x52, 0x52, 0x3E}},
    {'h', {0x7F, 0x08, 0x04, 0x04, 0x78}},
    {'i', {0x00, 0x44, 0x7D, 0x40, 0x00}},
    {'k', {0x7F, 0x10, 0x28, 0x44, 0x00}},
    {'m', {0x7C, 0x04, 0x18, 0x04, 0x78}},
    {'n', {0x7C, 0x08, 0x04, 0x04, 0x78}},
    {'o', {0x38, 0x44, 0x44, 0x44, 0x38}},
    {'p', {0x7C, 0x14, 0x14, 0x14, 0x08}},
    {'r', {0x7C, 0x08, 0x04, 0x04, 0x08}},
    {'s', {0x48, 0x54, 0x54, 0x54, 0x20}},
    {'t', {0x04, 0x3F, 0x44, 0x40, 0x20}},
    {'u', {0x3C, 0x40, 0x40, 0x20, 0x7C}},
    {'v', {0x1C, 0x20, 0x40, 0x20, 0x1C}},
    {'y', {0x0C, 0x50, 0x50, 0x50, 0x3C}},
};

/* ui_sensor_page_ms_to_ticks：把毫秒时间转换为 FreeRTOS tick。
 *
 * 参数：
 *     ms：需要转换的毫秒数。
 *
 * 功能：
 *     Sensor 页面内部只用它计算 UI_MAIN_SENSOR_REFRESH_MS 的刷新间隔；当宏值很小导致
 *     pdMS_TO_TICKS() 转换为 0 时，至少返回 1 tick，避免周期判断永远停在 0。
 *
 * 调用方法：
 *     TickType_t refresh_ticks = ui_sensor_page_ms_to_ticks(UI_MAIN_SENSOR_REFRESH_MS);
 *
 * 返回：
 *     可直接用于 xTaskGetTickCount() 差值比较的 tick 数。
 */
static TickType_t ui_sensor_page_ms_to_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    return ticks;
}

/* ui_sensor_page_find_glyph：查找 Sensor 数据区小号字体点阵。
 *
 * 参数：
 *     ch：需要显示的 ASCII 字符。
 *
 * 功能：
 *     在 Sensor 页面私有小号字库中查找字符点阵。如果字符未收录，返回
 *     UI_MAIN_SMALL_FONT_FALLBACK_CHAR 对应点阵，避免显示函数访问空指针。
 *
 * 调用方法：
 *     const uint8_t *glyph = ui_sensor_page_find_glyph('T');
 *
 * 返回：
 *     UI_MAIN_SMALL_FONT_WIDTH 列点阵数据。
 */
static const uint8_t *ui_sensor_page_find_glyph(char ch)
{
    const uint8_t *fallback = s_ui_sensor_page_font5x7[0].column;

    for (size_t i = 0; i < (sizeof(s_ui_sensor_page_font5x7) / sizeof(s_ui_sensor_page_font5x7[0])); i++)
    {
        if (s_ui_sensor_page_font5x7[i].ch == UI_MAIN_SMALL_FONT_FALLBACK_CHAR)
        {
            fallback = s_ui_sensor_page_font5x7[i].column;
        }

        if (s_ui_sensor_page_font5x7[i].ch == ch)
        {
            return s_ui_sensor_page_font5x7[i].column;
        }
    }

    return fallback;
}

/* ui_sensor_page_draw_small_char：绘制 Sensor 数据区小号 ASCII 字符。
 *
 * 参数：
 *     x/y：字符左上角坐标；
 *     ch：需要显示的字符；
 *     color：字符前景色。
 *
 * 功能：
 *     使用 UI_MAIN_SMALL_FONT_WIDTH x UI_MAIN_SMALL_FONT_HEIGHT 点阵和 lcd_fill_rect() 绘制小号字符。
 *     该函数只服务 Sensor 数据文本，不修改 LCD 底层驱动，也不影响 HOME 页面和按键文字仍然使用
 *     原有 lcd_draw_string()。
 *
 * 调用方法：
 *     ui_sensor_page_draw_small_char(0, 120, 'T', UI_MAIN_SENSOR_DATA_TEXT_COLOR);
 *
 * 返回：
 *     ESP_OK：字符绘制成功或字符起点已经超出屏幕；
 *     ESP_ERR_INVALID_ARG：字体缩放参数为 0；
 *     其它值：lcd_fill_rect() 返回的错误码。
 */
static esp_err_t ui_sensor_page_draw_small_char(uint16_t x, uint16_t y, char ch, uint16_t color)
{
    const uint16_t font_width = UI_MAIN_SMALL_FONT_WIDTH;
    const uint16_t font_height = UI_MAIN_SMALL_FONT_HEIGHT;
    const uint16_t scale_x = UI_MAIN_SENSOR_DATA_FONT_SCALE_X;
    const uint16_t scale_y = UI_MAIN_SENSOR_DATA_FONT_SCALE_Y;

    if ((scale_x == 0U) || (scale_y == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((x >= LCD_H_RES) || (y >= LCD_V_RES))
    {
        return ESP_OK;
    }

    const uint8_t *glyph = ui_sensor_page_find_glyph(ch);

    for (uint16_t row = 0; row < font_height; row++)
    {
        uint16_t run_start = 0U;
        bool in_run = false;

        for (uint16_t col = 0; col <= font_width; col++)
        {
            bool draw_pixel = false;

            if (col < font_width)
            {
                draw_pixel = ((glyph[col] >> row) & 0x01U) != 0U;
            }

            if (draw_pixel && !in_run)
            {
                run_start = col;
                in_run = true;
            }
            else if (!draw_pixel && in_run)
            {
                const uint16_t run_x = (uint16_t)(x + (run_start * scale_x));
                const uint16_t run_y = (uint16_t)(y + (row * scale_y));
                const uint16_t run_w = (uint16_t)((col - run_start) * scale_x);

                in_run = false;

                if ((run_x >= LCD_H_RES) || (run_y >= LCD_V_RES))
                {
                    continue;
                }

                esp_err_t ret = lcd_fill_rect(run_x, run_y, run_w, scale_y, color);
                if (ret != ESP_OK)
                {
                    return ret;
                }
            }
        }
    }

    return ESP_OK;
}

/* ui_sensor_page_draw_small_string：绘制 Sensor 数据区小号 ASCII 字符串。
 *
 * 参数：
 *     x/y：字符串第一行左上角坐标；
 *     str：以 '\0' 结尾的 ASCII 字符串，不能为 NULL；
 *     color：字符串前景色。
 *
 * 功能：
 *     按 UI_MAIN_SENSOR_DATA_FONT_* 宏定义逐字绘制字符串。该函数不会自动整屏清屏，
 *     调用者需要先覆盖 UI_MAIN_SENSOR_CLEAR_* 数据区域，确保旧数据不会残留。
 *
 * 调用方法：
 *     ui_sensor_page_draw_small_string(UI_MAIN_SENSOR_DATA_X,
 *                                      UI_MAIN_SENSOR_DATA_Y,
 *                                      UI_MAIN_SENSOR_HUMI_FMT 格式化后的字符串,
 *                                      UI_MAIN_SENSOR_DATA_TEXT_COLOR);
 *
 * 返回：
 *     ESP_OK：字符串绘制成功或已经绘制到屏幕底部；
 *     ESP_ERR_INVALID_ARG：str 为 NULL，或字体步进参数无效；
 *     其它值：字符绘制时 LCD 接口返回的错误码。
 */
static esp_err_t ui_sensor_page_draw_small_string(uint16_t x,
                                                  uint16_t y,
                                                  const char *str,
                                                  uint16_t color)
{
    if (str == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t char_step_x = (uint16_t)((UI_MAIN_SMALL_FONT_WIDTH + UI_MAIN_SENSOR_DATA_FONT_SPACE_X) *
                                           UI_MAIN_SENSOR_DATA_FONT_SCALE_X);
    const uint16_t char_step_y = (uint16_t)((UI_MAIN_SMALL_FONT_HEIGHT + UI_MAIN_SENSOR_DATA_FONT_SPACE_Y) *
                                           UI_MAIN_SENSOR_DATA_FONT_SCALE_Y);

    if ((char_step_x == 0U) || (char_step_y == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t cursor_x = x;
    uint16_t cursor_y = y;

    while (*str != '\0')
    {
        if (*str == '\n')
        {
            cursor_x = x;
            cursor_y = (uint16_t)(cursor_y + char_step_y);
            str++;
            continue;
        }

        if (cursor_y >= LCD_V_RES)
        {
            return ESP_OK;
        }

        if (cursor_x < LCD_H_RES)
        {
            esp_err_t ret = ui_sensor_page_draw_small_char(cursor_x, cursor_y, *str, color);
            if (ret != ESP_OK)
            {
                return ret;
            }
        }

        cursor_x = (uint16_t)(cursor_x + char_step_x);
        str++;
    }

    return ESP_OK;
}

/* ui_sensor_page_clear_data_area：局部覆盖 Sensor 数据文本区域。
 *
 * 功能：
 *     每次刷新数据前只覆盖 UI_MAIN_SENSOR_CLEAR_* 定义的矩形区域，不调用 lcd_clear()，
 *     这样 Back 按键、标题和页面背景不会每秒重绘，能减少闪烁。
 *
 * 调用方法：
 *     ui_sensor_page_clear_data_area();    // 在绘制 not-ready 提示或四行真实数据前调用
 *
 * 返回：
 *     ESP_OK：局部覆盖成功；
 *     其它值：lcd_fill_rect() 返回的错误码。
 */
static esp_err_t ui_sensor_page_clear_data_area(void)
{
    return lcd_fill_rect(UI_MAIN_SENSOR_CLEAR_X,
                         UI_MAIN_SENSOR_CLEAR_Y,
                         UI_MAIN_SENSOR_CLEAR_W,
                         UI_MAIN_SENSOR_CLEAR_H,
                         UI_MAIN_SENSOR_DATA_BG_COLOR);
}

/* ui_sensor_page_draw_values：把 ENV 缓存数据绘制成 Sensor 页面四行文本。
 *
 * 参数：
 *     data：env_get_data() 返回的环境数据缓存，不能为 NULL。
 *
 * 功能：
 *     按指定格式显示真实环境测试数据：
 *     Temperature 使用 data->temperature_c；
 *     Humidity 使用 data->humidity_percent；
 *     Pressure 使用 data->pressure_hpa；
 *     Gas 使用 data->gas_resistance_ohm / 1000.0f 转成 kOhm。
 *
 * 调用方法：
 *     env_data_t data = {0};
 *     if (env_get_data(&data) == ESP_OK) {
 *         ui_sensor_page_draw_values(&data);
 *     }
 *
 * 返回：
 *     ESP_OK：四行数据绘制成功；
 *     ESP_ERR_INVALID_ARG：data 为 NULL；
 *     其它值：小号字符串绘制返回的错误码。
 */
static esp_err_t ui_sensor_page_draw_values(const env_data_t *data)
{
    if (data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char line[UI_MAIN_SENSOR_LINE_BUF_SIZE] = {0};
    const uint16_t x = UI_MAIN_SENSOR_DATA_X;
    uint16_t y = UI_MAIN_SENSOR_DATA_Y;

    snprintf(line, sizeof(line), UI_MAIN_SENSOR_TEMP_FMT, data->temperature_c);
    esp_err_t ret = ui_sensor_page_draw_small_string(x, y, line, UI_MAIN_SENSOR_DATA_TEXT_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    y = (uint16_t)(y + UI_MAIN_SENSOR_LINE_GAP);
    snprintf(line, sizeof(line), UI_MAIN_SENSOR_HUMI_FMT, data->humidity_percent);
    ret = ui_sensor_page_draw_small_string(x, y, line, UI_MAIN_SENSOR_DATA_TEXT_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    y = (uint16_t)(y + UI_MAIN_SENSOR_LINE_GAP);
    snprintf(line, sizeof(line), UI_MAIN_SENSOR_PRESS_FMT, data->pressure_hpa);
    ret = ui_sensor_page_draw_small_string(x, y, line, UI_MAIN_SENSOR_DATA_TEXT_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    y = (uint16_t)(y + UI_MAIN_SENSOR_LINE_GAP);
    snprintf(line, sizeof(line), UI_MAIN_SENSOR_GAS_FMT, ((float)data->gas_resistance_ohm) / 1000.0f);
    return ui_sensor_page_draw_small_string(x, y, line, UI_MAIN_SENSOR_DATA_TEXT_COLOR);
}

/* ui_sensor_page_draw_back_button：绘制 Sensor 页面左上角 Back 返回按键。
 *
 * 功能：
 *     该函数只负责绘制 Back 按键，不处理触摸事件。触摸命中判断由
 *     ui_sensor_page_is_back_hit() 完成，页面切换仍由 ui_main.c 完成。
 *
 * 调用方法：
 *     ui_sensor_page_draw();      // Sensor 页面完整重绘时会调用本函数
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：LCD 绘图接口返回的错误码。
 */
static esp_err_t ui_sensor_page_draw_back_button(void)
{
    esp_err_t ret = lcd_fill_rect(UI_MAIN_BACK_BTN_X,
                                  UI_MAIN_BACK_BTN_Y,
                                  UI_MAIN_BACK_BTN_W,
                                  UI_MAIN_BACK_BTN_H,
                                  UI_MAIN_BACK_BTN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_draw_string(UI_MAIN_BACK_BTN_TEXT_X,
                           UI_MAIN_BACK_BTN_TEXT_Y,
                           UI_MAIN_BACK_BTN_TEXT,
                           UI_MAIN_BUTTON_TEXT_COLOR,
                           UI_MAIN_BACK_BTN_COLOR);
}

/* ui_sensor_page_draw：绘制 Sensor 页面并立即刷新一次数据。
 *
 * 功能：
 *     1. 切换到 Sensor 页面时执行一次整屏清屏，随后绘制 Back 按键和 Sensor Data 标题；
 *     2. 静态内容绘制成功后，立即调用 ui_sensor_page_update(true) 强制刷新一次数据文本区域；
 *     3. 本函数只负责 Sensor 页面内部绘制，不维护 UI_PAGE_HOME/UI_PAGE_SENSOR 页面状态。
 *
 * 调用方法：
 *     ui_main_switch_page(UI_PAGE_SENSOR);    // ui_main.c 在页面切换到 Sensor 时调用本函数
 *
 * 返回：
 *     ESP_OK：静态页面和首次数据区域刷新成功；
 *     其它值：LCD 绘图接口或数据区域刷新过程返回的错误码。
 */
esp_err_t ui_sensor_page_draw(void)
{
    esp_err_t ret = lcd_clear(UI_MAIN_SCREEN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = ui_sensor_page_draw_back_button();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_draw_string(UI_MAIN_SENSOR_TITLE_X,
                          UI_MAIN_SENSOR_TITLE_Y,
                          UI_MAIN_SENSOR_TITLE_TEXT,
                          UI_MAIN_TEXT_COLOR,
                          UI_MAIN_SCREEN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return ui_sensor_page_update(true);
}

/* ui_sensor_page_update：刷新 Sensor 页面环境数据文本区域。
 *
 * 参数：
 *     force：
 *         true  表示强制刷新，不等待 UI_MAIN_SENSOR_REFRESH_MS 周期；
 *         false 表示普通周期刷新，未到 UI_MAIN_SENSOR_REFRESH_MS 周期时直接返回 ESP_OK。
 *
 * 功能：
 *     1. 使用 env_get_data(&data) 获取 ENV 模块缓存数据，不直接调用 BME690 底层驱动；
 *     2. 每次实际刷新前，只覆盖 UI_MAIN_SENSOR_CLEAR_* 定义的数据文本区域，不整屏清屏；
 *     3. ENV 数据未准备好时显示 UI_MAIN_SENSOR_NOT_READY_TEXT，不崩溃、不阻塞触摸事件。
 *
 * 调用方法：
 *     ui_sensor_page_update(true);     // 页面刚进入 Sensor 后需要立即显示数据时调用
 *     ui_sensor_page_update(false);    // UI 主循环中、当前页面为 Sensor 时周期调用
 *
 * 返回：
 *     ESP_OK：未到刷新周期或刷新成功；
 *     其它值：LCD 局部覆盖或文本绘制返回的错误码。
 */
esp_err_t ui_sensor_page_update(bool force)
{
    static TickType_t s_last_refresh_ticks = 0;

    const TickType_t now_ticks = xTaskGetTickCount();
    const TickType_t refresh_ticks = ui_sensor_page_ms_to_ticks(UI_MAIN_SENSOR_REFRESH_MS);

    if (!force && ((now_ticks - s_last_refresh_ticks) < refresh_ticks))
    {
        return ESP_OK;
    }

    env_data_t data = {0};
    esp_err_t env_ret = env_get_data(&data);

    esp_err_t ret = ui_sensor_page_clear_data_area();
    if (ret != ESP_OK)
    {
        return ret;
    }

    if ((env_ret != ESP_OK) || !data.valid)
    {
        ret = ui_sensor_page_draw_small_string(UI_MAIN_SENSOR_DATA_X,
                                               UI_MAIN_SENSOR_DATA_Y,
                                               UI_MAIN_SENSOR_NOT_READY_TEXT,
                                               UI_MAIN_SENSOR_DATA_TEXT_COLOR);
    }
    else
    {
        ret = ui_sensor_page_draw_values(&data);
    }

    if (ret == ESP_OK)
    {
        s_last_refresh_ticks = now_ticks;
    }

    return ret;
}

/* ui_sensor_page_is_back_hit：判断触摸点是否命中 Sensor 页面的 Back 按键。
 *
 * 参数：
 *     x：已经由 ui_main.c 映射后的 LCD X 坐标；
 *     y：已经由 ui_main.c 映射后的 LCD Y 坐标。
 *
 * 功能：
 *     只判断坐标是否落入 UI_MAIN_BACK_BTN_* 定义的 Back 按键矩形区域，不读取触摸底层，
 *     不切换页面，也不刷新 Sensor 数据。
 *
 * 调用方法：
 *     if (ui_sensor_page_is_back_hit(x, y)) {
 *         ui_main_switch_page(UI_PAGE_HOME);
 *     }
 *
 * 返回：
 *     true：触摸点命中 Back 按键；
 *     false：触摸点未命中 Back 按键。
 */
bool ui_sensor_page_is_back_hit(int x, int y)
{
    if ((x < 0) || (y < 0))
    {
        return false;
    }

    const uint32_t point_x = (uint32_t)x;
    const uint32_t point_y = (uint32_t)y;
    const uint32_t left = UI_MAIN_BACK_BTN_X;
    const uint32_t top = UI_MAIN_BACK_BTN_Y;
    const uint32_t right = left + UI_MAIN_BACK_BTN_W;
    const uint32_t bottom = top + UI_MAIN_BACK_BTN_H;

    return (point_x >= left) &&
           (point_x < right) &&
           (point_y >= top) &&
           (point_y < bottom);
}
