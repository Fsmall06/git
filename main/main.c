#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "env.h"

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

    while (1)
    {
        ESP_LOGI(TAG, "alive");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
