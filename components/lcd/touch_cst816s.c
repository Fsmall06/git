#include "touch_cst816s.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* s_touch_iic：CST816S 当前复用的 I2C 总线对象指针。
 *
 * 说明：
 *     该指针只在本驱动内部使用，外部业务不需要关心 BSP/IIC 的具体句柄。
 *     touch_init() 初始化成功后赋值，touch_read() 每次读取前检查它是否有效。
 */
static i2c_obj_t *s_touch_iic = NULL;

/* touch_get_iic_bus：初始化并取得 CST816S 使用的 BSP/IIC 总线对象。
 *
 * 功能：
 *     本函数只复用现有 BSP/IIC 驱动，不重新配置 SDA/SCL、I2C 频率或底层驱动。
 *     这里明确不做地址扫描、不读 Chip ID，避免重新引入探测逻辑。
 *
 * 参数：
 *     bus：输出参数，成功时指向 iic_master[TOUCH_CST816S_IIC_PORT]。
 *
 * 调用方法：
 *     i2c_obj_t *bus = NULL;
 *     esp_err_t ret = touch_get_iic_bus(&bus);
 *
 * 返回：
 *     ESP_OK：I2C 总线已经可用；
 *     ESP_ERR_INVALID_ARG：bus 为空或 I2C 端口配置非法；
 *     ESP_ERR_INVALID_STATE：I2C 总线状态异常；
 *     其它值：iic_init() 返回的初始化错误。
 */
static esp_err_t touch_get_iic_bus(i2c_obj_t **bus)
{
    if (bus == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *bus = NULL;

    if (TOUCH_CST816S_IIC_PORT >= I2C_NUM_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_obj_t iic_obj = iic_init(TOUCH_CST816S_IIC_PORT);
    if (iic_obj.init_flag != ESP_OK)
    {
        return iic_obj.init_flag;
    }

    *bus = &iic_master[TOUCH_CST816S_IIC_PORT];
    if (((*bus)->init_flag != ESP_OK) || ((*bus)->bus_handle == NULL))
    {
        *bus = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/* touch_read_point_regs：读取 CST816S 触摸点基础寄存器。
 *
 * 功能：
 *     从 0x03 开始连续读取 5 字节，覆盖 FingerNum、XposH、XposL、YposH、YposL。
 *     寄存器地址和读取长度都在 touch_cst816s.h 中定义，便于后期调试。
 *
 * 参数：
 *     buf：输出缓冲区，长度必须不小于 TOUCH_CST816S_POINT_DATA_LEN。
 *
 * 调用方法：
 *     uint8_t data[TOUCH_CST816S_POINT_DATA_LEN] = {0};
 *     esp_err_t ret = touch_read_point_regs(data);
 *
 * 返回：
 *     ESP_OK：读取成功；
 *     ESP_ERR_INVALID_ARG：buf 为空；
 *     ESP_ERR_INVALID_STATE：touch_init() 尚未成功执行；
 *     其它值：iic_read() 返回的 I2C 通信错误。
 */
static esp_err_t touch_read_point_regs(uint8_t *buf)
{
    if (buf == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((s_touch_iic == NULL) ||
        (s_touch_iic->init_flag != ESP_OK) ||
        (s_touch_iic->bus_handle == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t start_reg = TOUCH_CST816S_REG_FINGER_NUM;

    return iic_read(s_touch_iic,
                    TOUCH_CST816S_IIC_ADDR,
                    &start_reg,
                    sizeof(start_reg),
                    buf,
                    TOUCH_CST816S_POINT_DATA_LEN);
}

esp_err_t touch_init(void)
{
    i2c_obj_t *bus = NULL;
    esp_err_t ret = touch_get_iic_bus(&bus);
    if (ret != ESP_OK)
    {
        s_touch_iic = NULL;
        return ret;
    }

    s_touch_iic = bus;
    return ESP_OK;
}

esp_err_t touch_read(uint16_t *x, uint16_t *y, bool *pressed)
{
    if ((x == NULL) || (y == NULL) || (pressed == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *x = 0;
    *y = 0;
    *pressed = false;

    uint8_t data[TOUCH_CST816S_POINT_DATA_LEN] = {0};
    esp_err_t ret = touch_read_point_regs(data);
    if (ret != ESP_OK)
    {
        return ret;
    }

    const uint8_t finger_num = data[0];
    if (finger_num == 0)
    {
        return ESP_OK;
    }

    const uint8_t xh = data[1];
    const uint8_t xl = data[2];
    const uint8_t yh = data[3];
    const uint8_t yl = data[4];

    *x = (uint16_t)((((uint16_t)xh & TOUCH_CST816S_COORD_HIGH_MASK)
                    << TOUCH_CST816S_COORD_HIGH_SHIFT) |
                   (uint16_t)xl);
    *y = (uint16_t)((((uint16_t)yh & TOUCH_CST816S_COORD_HIGH_MASK)
                    << TOUCH_CST816S_COORD_HIGH_SHIFT) |
                   (uint16_t)yl);
    *pressed = true;

    return ESP_OK;
}

void touch_test_task(void *arg)
{
    (void)arg;

    if ((s_touch_iic == NULL) ||
        (s_touch_iic->init_flag != ESP_OK) ||
        (s_touch_iic->bus_handle == NULL))
    {
        (void)touch_init();
    }

    while (1)
    {
        uint16_t x = 0;
        uint16_t y = 0;
        bool pressed = false;

        if ((touch_read(&x, &y, &pressed) == ESP_OK) && pressed)
        {
            printf("TOUCH:\npressed=1\nx=%u\ny=%u\n",
                   (unsigned int)x,
                   (unsigned int)y);
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_CST816S_TEST_PERIOD_MS));
    }
}
