#include "touch_test.h"

#include "esp_log.h"

static const char *TAG = TOUCH_TEST_LOG_TAG;

/* touch_test_get_iic_bus：初始化并取得现有 BSP/IIC 总线对象。
 *
 * 功能：
 *     本函数只复用现有 IIC 驱动，不重新配置 SDA/SCL、I2C 频率或底层驱动。
 *     iic_init() 内部已经处理重复初始化，因此可以在 BME690、ENV 或扫描流程之后调用。
 *
 * 参数：
 *     bus：输出参数，成功时指向 iic_master[TOUCH_TEST_IIC_PORT]。
 *
 * 调用方法：
 *     i2c_obj_t *bus = NULL;
 *     esp_err_t ret = touch_test_get_iic_bus(&bus);
 *     if (ret == ESP_OK) {
 *         // 使用 bus 调用 iic_read()
 *     }
 *
 * 返回：
 *     ESP_OK：I2C 总线已经可用；
 *     ESP_ERR_INVALID_ARG：bus 参数为空，或 TOUCH_TEST_IIC_PORT 超出范围；
 *     其它值：iic_init() 返回的初始化错误。
 */
static esp_err_t touch_test_get_iic_bus(i2c_obj_t **bus)
{
    if (bus == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *bus = NULL;

    if (TOUCH_TEST_IIC_PORT >= I2C_NUM_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_obj_t iic_obj = iic_init(TOUCH_TEST_IIC_PORT);
    if (iic_obj.init_flag != ESP_OK)
    {
        return iic_obj.init_flag;
    }

    *bus = &iic_master[TOUCH_TEST_IIC_PORT];
    if (((*bus)->init_flag != ESP_OK) || ((*bus)->bus_handle == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/* touch_test_read_reg：读取触摸芯片单个寄存器的 1 字节数据。
 *
 * 功能：
 *     使用现有 iic_read() 完成标准寄存器读取时序：
 *     1. 先向 TOUCH_TEST_IIC_ADDR 写入 1 字节寄存器地址；
 *     2. I2C 驱动产生 repeated start；
 *     3. 从同一设备地址读取 TOUCH_TEST_READ_LEN 字节。
 *
 * 参数：
 *     bus：已经初始化完成的 BSP/IIC 总线对象；
 *     reg：要读取的触摸芯片寄存器地址；
 *     value：输出参数，保存读取到的 1 字节寄存器值。
 *
 * 调用方法：
 *     uint8_t value = 0;
 *     esp_err_t ret = touch_test_read_reg(bus, TOUCH_TEST_REG_A7, &value);
 *
 * 返回：
 *     ESP_OK：读取成功；
 *     ESP_ERR_INVALID_ARG：bus 或 value 参数为空；
 *     其它值：iic_read() 返回的 I2C 通信错误。
 */
static esp_err_t touch_test_read_reg(i2c_obj_t *bus, uint8_t reg, uint8_t *value)
{
    if ((bus == NULL) || (value == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return iic_read(bus,
                    TOUCH_TEST_IIC_ADDR,
                    &reg,
                    sizeof(reg),
                    value,
                    TOUCH_TEST_READ_LEN);
}

esp_err_t touch_test_probe(void)
{
    i2c_obj_t *bus = NULL;
    esp_err_t ret = touch_test_get_iic_bus(&bus);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "TOUCH: read failed");
        return ret;
    }

    /* 寄存器表统一放在这里，循环逻辑只关心表内容。
     * 后续如果要临时增减测试寄存器，优先修改 touch_test.h 中的宏，再同步调整本数组。
     */
    const uint8_t regs[TOUCH_TEST_REG_COUNT] = {
        TOUCH_TEST_REG_A7,
        TOUCH_TEST_REG_A8,
        TOUCH_TEST_REG_A9,
    };

    for (uint8_t i = 0; i < TOUCH_TEST_REG_COUNT; i++)
    {
        uint8_t value = 0;

        ret = touch_test_read_reg(bus, regs[i], &value);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "TOUCH: read failed");
            return ret;
        }

        ESP_LOGI(TAG, "TOUCH: reg 0x%02X = 0x%02X", (unsigned int)regs[i], (unsigned int)value);
    }

    return ESP_OK;
}
