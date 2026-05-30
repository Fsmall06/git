#include "lcd_ui.h"

#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "LCD_UI";

/* lcd_ui_draw_hline：绘制水平分割线。
 *
 * 参数：
 *     x：分割线起始 X 坐标。
 *     y：分割线起始 Y 坐标。
 *     width：分割线宽度，单位像素。
 *     color：RGB565 颜色。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：底层 draw_pixel 返回的错误。
 *
 * 调用方法：
 *     lcd_ui_draw_hline(LCD_UI_MARGIN_X, LCD_UI_TITLE_LINE_Y, LCD_UI_LINE_WIDTH, LCD_UI_LINE_COLOR);
 */
static esp_err_t lcd_ui_draw_hline(uint16_t x, uint16_t y, uint16_t width, uint16_t color)
{
    for (uint16_t i = 0; i < width; i++)
    {
        esp_err_t ret = lcd_st7789_draw_pixel((uint16_t)(x + i), y, color);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

/* lcd_ui_draw_label_value：绘制一行“标签 + 数值 + 单位”。
 *
 * 参数：
 *     row_index：行索引，0 为 TEMP 行。
 *     label：标签文本，不能为 NULL。
 *     value：需要显示的数值。
 *     decimals：小数位数。
 *     unit：单位文本，不能为 NULL。
 *     value_color：数值颜色。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：底层绘图错误。
 *
 * 调用方法：
 *     lcd_ui_draw_label_value(0, "TEMP", 25.6f, 1, "^C", LCD_UI_VALUE_COLOR);
 */
static esp_err_t lcd_ui_draw_label_value(uint8_t row_index,
                                         const char *label,
                                         float value,
                                         uint8_t decimals,
                                         const char *unit,
                                         uint16_t value_color)
{
    if ((label == NULL) || (unit == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t y = (uint16_t)(LCD_UI_ROW_START_Y + ((uint16_t)row_index * LCD_UI_ROW_GAP));

    esp_err_t ret = lcd_st7789_draw_string(LCD_UI_LABEL_X,
                                           y,
                                           label,
                                           &g_lcd_font16,
                                           LCD_UI_LABEL_COLOR,
                                           LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_draw_number(LCD_UI_VALUE_X,
                                 (uint16_t)(y - 4U),
                                 value,
                                 decimals,
                                 &g_lcd_font24,
                                 value_color,
                                 LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_st7789_draw_string(LCD_UI_UNIT_X,
                                  y,
                                  unit,
                                  &g_lcd_font16,
                                  LCD_UI_LABEL_COLOR,
                                  LCD_UI_BG_COLOR);
}

/* lcd_ui_draw_state_pair：绘制一个状态键值对。
 *
 * 参数：
 *     x/y：状态块左上角坐标。
 *     label：状态标签，例如 "WIFI"。
 *     state：状态布尔值。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：底层绘图错误。
 *
 * 调用方法：
 *     lcd_ui_draw_state_pair(8, 188, "WIFI", true);
 */
static esp_err_t lcd_ui_draw_state_pair(uint16_t x, uint16_t y, const char *label, bool state)
{
    if (label == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = lcd_st7789_draw_string(x,
                                           y,
                                           label,
                                           &g_lcd_font16,
                                           LCD_UI_LABEL_COLOR,
                                           LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    const char *state_text = state ? "ON" : "OFF";
    const uint16_t state_color = state ? LCD_UI_STATE_ON_COLOR : LCD_UI_STATE_OFF_COLOR;
    const uint16_t state_x = (uint16_t)(x + (5U * g_lcd_font16.width));

    return lcd_st7789_draw_string(state_x,
                                  y,
                                  state_text,
                                  &g_lcd_font16,
                                  state_color,
                                  LCD_UI_BG_COLOR);
}

esp_err_t lcd_ui_draw_dashboard(const lcd_ui_dashboard_data_t *data)
{
    if (data == NULL)
    {
        ESP_LOGE(TAG, "Dashboard 数据为空");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = lcd_st7789_fill_screen(LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_draw_string(LCD_UI_TITLE_X,
                                 LCD_UI_TITLE_Y,
                                 "AI DASHBOARD",
                                 &g_lcd_font16,
                                 LCD_UI_TITLE_COLOR,
                                 LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_hline(LCD_UI_MARGIN_X, LCD_UI_TITLE_LINE_Y, LCD_UI_LINE_WIDTH, LCD_UI_LINE_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_label_value(0,
                                  "TEMP",
                                  data->temperature_c,
                                  LCD_UI_TEMP_DECIMALS,
                                  "^C",
                                  LCD_UI_VALUE_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_label_value(1,
                                  "HUM",
                                  data->humidity_percent,
                                  LCD_UI_HUM_DECIMALS,
                                  "%",
                                  LCD_UI_VALUE_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_label_value(2,
                                  "PRESS",
                                  data->pressure_hpa,
                                  LCD_UI_PRESS_DECIMALS,
                                  "HPA",
                                  LCD_UI_VALUE_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_label_value(3,
                                  "GAS",
                                  (float)data->gas_resistance_ohm / LCD_UI_GAS_KOHM_DIVISOR,
                                  LCD_UI_GAS_DECIMALS,
                                  "KOHM",
                                  LCD_UI_ACCENT_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_hline(LCD_UI_MARGIN_X, LCD_UI_STATUS_LINE_Y, LCD_UI_LINE_WIDTH, LCD_UI_LINE_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_state_pair(LCD_UI_STATUS_LEFT_X, LCD_UI_STATUS_Y, "WIFI", data->wifi_connected);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_ui_draw_state_pair(LCD_UI_STATUS_RIGHT_X, LCD_UI_STATUS_Y, "ASR", data->asr_active);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_draw_string(LCD_UI_MARGIN_X,
                                 LCD_UI_VALID_Y,
                                 "ENV",
                                 &g_lcd_font16,
                                 LCD_UI_LABEL_COLOR,
                                 LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_draw_string((uint16_t)(LCD_UI_MARGIN_X + (4U * g_lcd_font16.width)),
                                 LCD_UI_VALID_Y,
                                 data->env_valid ? "OK" : "WAIT",
                                 &g_lcd_font16,
                                 data->env_valid ? LCD_UI_STATE_ON_COLOR : LCD_UI_STATE_OFF_COLOR,
                                 LCD_UI_BG_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_st7789_flush();
}
