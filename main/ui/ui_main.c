#include "ui_main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"
#include "touch_cst816t.h"

static const char *TAG = "UI_MAIN";

#define UI_MAIN_TASK_NAME "ui_button_task"
#define UI_MAIN_TASK_STACK_SIZE 4096U
#define UI_MAIN_TASK_PRIORITY 5U
#define UI_MAIN_POLL_PERIOD_MS 20U

#define UI_MAIN_TOUCH_X_MIN 14
#define UI_MAIN_TOUCH_X_MAX 226
#define UI_MAIN_TOUCH_Y_MIN 35
#define UI_MAIN_TOUCH_Y_MAX 271

#define UI_MAIN_BUTTON_WIDTH 132U
#define UI_MAIN_BUTTON_HEIGHT 58U
#define UI_MAIN_BUTTON_X ((uint16_t)((LCD_H_RES - UI_MAIN_BUTTON_WIDTH) / 2U))
#define UI_MAIN_BUTTON_Y ((uint16_t)(((LCD_V_RES - UI_MAIN_BUTTON_HEIGHT) / 2U) + 18U))

#define UI_MAIN_SCREEN_COLOR LCD_COLOR_BLACK
#define UI_MAIN_BUTTON_BLUE LCD_COLOR_BLUE
#define UI_MAIN_BUTTON_RED LCD_COLOR_RED
#define UI_MAIN_BUTTON_TEXT_COLOR LCD_COLOR_WHITE

static TaskHandle_t s_ui_task_handle = NULL;
static bool s_button_red = false;

static TickType_t ui_main_ms_to_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    return ticks;
}

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

static bool ui_main_point_in_button(uint16_t x, uint16_t y)
{
    return (x >= UI_MAIN_BUTTON_X) &&
           (x < (UI_MAIN_BUTTON_X + UI_MAIN_BUTTON_WIDTH)) &&
           (y >= UI_MAIN_BUTTON_Y) &&
           (y < (UI_MAIN_BUTTON_Y + UI_MAIN_BUTTON_HEIGHT));
}

static esp_err_t ui_main_draw_button(void)
{
    const uint16_t color = s_button_red ? UI_MAIN_BUTTON_RED : UI_MAIN_BUTTON_BLUE;

    esp_err_t ret = lcd_fill_rect(UI_MAIN_BUTTON_X,
                                  UI_MAIN_BUTTON_Y,
                                  UI_MAIN_BUTTON_WIDTH,
                                  UI_MAIN_BUTTON_HEIGHT,
                                  color);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_draw_string((uint16_t)(UI_MAIN_BUTTON_X + 30U),
                           (uint16_t)(UI_MAIN_BUTTON_Y + 22U),
                           "BUTTON",
                           UI_MAIN_BUTTON_TEXT_COLOR,
                           color);
}

static esp_err_t ui_main_draw_screen(void)
{
    esp_err_t ret = lcd_clear(UI_MAIN_SCREEN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_draw_string(42U,
                          28U,
                          "Touch Button",
                          UI_MAIN_BUTTON_TEXT_COLOR,
                          UI_MAIN_SCREEN_COLOR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return ui_main_draw_button();
}

static void ui_main_task(void *arg)
{
    (void)arg;

    bool touch_active = false;
    bool button_press_active = false;

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

                if (ui_main_map_touch_point(raw_x, raw_y, &x, &y) &&
                    ui_main_point_in_button(x, y))
                {
                    button_press_active = true;
                    s_button_red = !s_button_red;

                    esp_err_t ret = ui_main_draw_button();
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "button redraw failed: %s", esp_err_to_name(ret));
                    }

                    printf("BUTTON PRESSED\n");
                }
            }
        }
        else if (read_ok)
        {
            if (button_press_active)
            {
                printf("BUTTON RELEASED\n");
            }

            touch_active = false;
            button_press_active = false;
        }

        vTaskDelay(ui_main_ms_to_ticks(UI_MAIN_POLL_PERIOD_MS));
    }
}

esp_err_t ui_main_start(void)
{
    if (s_ui_task_handle != NULL)
    {
        ESP_LOGI(TAG, "UI button task already running");
        return ESP_OK;
    }

    s_button_red = false;

    esp_err_t ret = ui_main_draw_screen();
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
        ESP_LOGE(TAG, "UI button task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UI button demo started");
    return ESP_OK;
}
