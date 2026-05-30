#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "st7789_min_test.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "ST7789 minimal init trace start");

    ESP_ERROR_CHECK(st7789_min_init());

    ESP_LOGI(TAG, "ST7789 minimal init trace done");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
