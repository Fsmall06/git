#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "env.h"
#include "lcd.h"
#include "touch_cst816t.h"
#include "touch_paint.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "APP start");

    esp_err_t lcd_ret = lcd_init();
    if (lcd_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "lcd_init failed: %s", esp_err_to_name(lcd_ret));
    }
    else
    {
        ESP_LOGI(TAG, "LCD started");
    }

    esp_err_t touch_ret = cst816t_init();
    if (touch_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "cst816t_init failed: %s", esp_err_to_name(touch_ret));
    }
    else
    {
        ESP_LOGI(TAG, "CST816T started");
    }

    if ((lcd_ret == ESP_OK) && (touch_ret == ESP_OK))
    {
        esp_err_t paint_ret = touch_paint_start();
        if (paint_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "touch_paint_start failed: %s", esp_err_to_name(paint_ret));
        }
        else
        {
            ESP_LOGI(TAG, "Touch Paint Demo started");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Touch Paint Demo skipped because LCD or CST816T init failed");
    }

    esp_err_t ret = env_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "env_init failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "ENV started");
    }

    while (1)
    {
        ESP_LOGI(TAG, "alive");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
