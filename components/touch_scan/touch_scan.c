#include "touch_scan.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = TOUCH_SCAN_LOG_TAG;

/* touch_scan_check_addr_range：检查 touch_scan 的扫描地址配置是否合法。
 *
 * 功能：
 *     1. 确认起始地址没有大于结束地址；
 *     2. 确认结束地址没有超过 7bit I2C 地址最大值；
 *     3. 在正式访问 I2C 总线前提前暴露宏配置错误，方便后期调试。
 *
 * 调用方法：
 *     esp_err_t ret = touch_scan_check_addr_range();
 *     if (ret != ESP_OK) {
 *         return ret;
 *     }
 *
 * 返回：
 *     ESP_OK：地址范围合法；
 *     ESP_ERR_INVALID_ARG：地址范围配置错误。
 */
static esp_err_t touch_scan_check_addr_range(void)
{
    if (TOUCH_SCAN_ADDR_START > TOUCH_SCAN_ADDR_END)
    {
        ESP_LOGE(TAG,
                 "touch_scan 地址范围错误，起始地址 0x%02X 大于结束地址 0x%02X",
                 (unsigned int)TOUCH_SCAN_ADDR_START,
                 (unsigned int)TOUCH_SCAN_ADDR_END);
        return ESP_ERR_INVALID_ARG;
    }

    if (TOUCH_SCAN_ADDR_END > IIC_ADDR_7BIT_MAX)
    {
        ESP_LOGE(TAG,
                 "touch_scan 地址范围错误，结束地址 0x%02X 超过 7bit 最大地址 0x%02X",
                 (unsigned int)TOUCH_SCAN_ADDR_END,
                 (unsigned int)IIC_ADDR_7BIT_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* touch_scan_get_iic_bus：初始化并取得现有 BSP/IIC 总线对象。
 *
 * 功能：
 *     本函数只调用现有 IIC 驱动的 iic_init()，不重新实现 I2C 初始化流程。
 *     iic_init() 内部已经处理重复初始化，因此 touch_scan_run() 可以在其它模块之后调用。
 *
 * 参数：
 *     bus：输出参数，成功时指向 iic_master[TOUCH_SCAN_IIC_PORT]。
 *
 * 调用方法：
 *     i2c_obj_t *bus = NULL;
 *     esp_err_t ret = touch_scan_get_iic_bus(&bus);
 *     if (ret == ESP_OK) {
 *         // 使用 bus->bus_handle 进行 I2C 探测
 *     }
 *
 * 返回：
 *     ESP_OK：I2C 总线已经可用；
 *     ESP_ERR_INVALID_ARG：bus 参数为空，或 TOUCH_SCAN_IIC_PORT 超出范围；
 *     其它值：iic_init() 返回的初始化错误。
 */
static esp_err_t touch_scan_get_iic_bus(i2c_obj_t **bus)
{
    if (bus == NULL)
    {
        ESP_LOGE(TAG, "touch_scan_get_iic_bus 参数错误，bus 不能为 NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *bus = NULL;

    if (TOUCH_SCAN_IIC_PORT >= I2C_NUM_MAX)
    {
        ESP_LOGE(TAG,
                 "touch_scan I2C 端口非法，port: %d，最大端口数量: %d",
                 (int)TOUCH_SCAN_IIC_PORT,
                 (int)I2C_NUM_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    i2c_obj_t iic_obj = iic_init(TOUCH_SCAN_IIC_PORT);
    if (iic_obj.init_flag != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "touch_scan 复用 IIC 驱动初始化失败，port: %d，ret: %s",
                 (int)TOUCH_SCAN_IIC_PORT,
                 esp_err_to_name(iic_obj.init_flag));
        return iic_obj.init_flag;
    }

    *bus = &iic_master[TOUCH_SCAN_IIC_PORT];
    if (((*bus)->init_flag != ESP_OK) || ((*bus)->bus_handle == NULL))
    {
        ESP_LOGE(TAG, "touch_scan I2C 总线状态异常，请先确认 BSP/IIC 初始化结果");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t touch_scan_run(void)
{
    esp_err_t ret = touch_scan_check_addr_range();
    if (ret != ESP_OK)
    {
        return ret;
    }

    i2c_obj_t *bus = NULL;
    ret = touch_scan_get_iic_bus(&bus);
    if (ret != ESP_OK)
    {
        return ret;
    }

    uint8_t found_num = 0;

    ESP_LOGI(TAG,
             "开始扫描触摸候选 I2C 地址，port: %d，范围: 0x%02X - 0x%02X，单地址超时: %d ms",
             (int)TOUCH_SCAN_IIC_PORT,
             (unsigned int)TOUCH_SCAN_ADDR_START,
             (unsigned int)TOUCH_SCAN_ADDR_END,
             TOUCH_SCAN_PROBE_TIMEOUT_MS);

    /* 使用现有 BSP/IIC 已创建好的 bus_handle 进行探测。
     * 这里没有重新配置 SDA/SCL、频率或安装 I2C 驱动，只是在既有总线上调用 ESP-IDF probe。
     * i2c_master_probe() 返回 ESP_OK 表示当前 7bit 地址有设备 ACK，需要按要求输出该地址。
     */
    for (uint16_t addr = TOUCH_SCAN_ADDR_START; addr <= TOUCH_SCAN_ADDR_END; addr++)
    {
        ret = i2c_master_probe(bus->bus_handle, addr, TOUCH_SCAN_PROBE_TIMEOUT_MS);
        if (ret == ESP_OK)
        {
            found_num++;
            ESP_LOGI(TAG, "Touch candidate found: 0x%02X", (unsigned int)addr);
        }
    }

    if (found_num == 0)
    {
        ESP_LOGI(TAG, "触摸候选 I2C 地址扫描完成，未发现设备");
    }
    else
    {
        ESP_LOGI(TAG, "触摸候选 I2C 地址扫描完成，共发现 %u 个地址", (unsigned int)found_num);
    }

    return ESP_OK;
}
