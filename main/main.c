#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "env.h"
#include "lcd.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "APP start");

    esp_err_t ret = env_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "env_init failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "ENV started");
    }

    esp_err_t lcd_ret = lcd_init();
    if (lcd_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "lcd_init failed: %s", esp_err_to_name(lcd_ret));
    }
    else
    {
        ESP_LOGI(TAG, "LCD started");

        lcd_clear(RED);
        vTaskDelay(pdMS_TO_TICKS(500));

        lcd_clear(GREEN);
        vTaskDelay(pdMS_TO_TICKS(500));

        lcd_clear(BLUE);
        vTaskDelay(pdMS_TO_TICKS(500));

        lcd_clear(WHITE);
        vTaskDelay(pdMS_TO_TICKS(500));

        lcd_draw_string(20, 40, "ESP32-C5", BLACK, WHITE);
        lcd_draw_string(20, 70, "SensAir Shuttle", BLACK, WHITE);
    }

    while (1)
    {
        ESP_LOGI(TAG, "alive");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
