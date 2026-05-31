#include "touch_cst816t.h"

#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = CST816T_LOG_TAG;

/* s_cst816t_iic：CST816T 当前复用的 BSP/IIC 总线对象。
 *
 * 说明：
 *     该指针只在本驱动内部使用。外部业务代码不需要知道 BSP/IIC 的具体句柄，
 *     只需要调用 cst816t_init() 和 cst816t_read_point()。
 */
static i2c_obj_t *s_cst816t_iic = NULL;

/* s_cst816t_dev：CST816T 在 ESP-IDF I2C 主机驱动中的设备句柄。
 *
 * 说明：
 *     cst816t_init() 创建该句柄后，后续读取寄存器直接复用它。
 *     这样触摸模块保持自己的 I2C 设备上下文，不会修改 BME690 或 LCD 的配置。
 */
static i2c_master_dev_handle_t s_cst816t_dev = NULL;

/* s_cst816t_ready：CST816T 驱动是否已经初始化成功。
 *
 * 说明：
 *     cst816t_read_point() 会先检查该标志，避免在 I2C 总线或设备句柄无效时访问硬件。
 */
static bool s_cst816t_ready = false;

/* cst816t_get_iic_bus：初始化并取得 CST816T 使用的 BSP/IIC 总线对象。
 *
 * 功能：
 *     1. 调用 iic_init(CST816T_IIC_PORT) 复用现有 I2C 初始化流程；
 *     2. 检查总线对象的 init_flag 和 bus_handle 是否有效；
 *     3. 不重新配置 LCD，不访问 BME690，也不做 I2C 地址扫描。
 *
 * 参数：
 *     bus：输出参数，成功时指向 iic_master[CST816T_IIC_PORT]。
 *
 * 调用方法：
 *     i2c_obj_t *bus = NULL;
 *     esp_err_t ret = cst816t_get_iic_bus(&bus);
 *
 * 返回：
 *     ESP_OK：I2C 总线已经可用；
 *     ESP_ERR_INVALID_ARG：bus 为空或 I2C 端口宏配置非法；
 *     ESP_ERR_INVALID_STATE：I2C 总线状态异常；
 *     其它值：iic_init() 返回的初始化错误。
 */
static esp_err_t cst816t_get_iic_bus(i2c_obj_t **bus)
{
    if (bus == NULL)
    {
        ESP_LOGE(TAG, "cst816t_get_iic_bus 参数错误，bus 不能为 NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *bus = NULL;

    if (CST816T_IIC_PORT >= I2C_NUM_MAX)
    {
        ESP_LOGE(TAG,
                 "CST816T I2C 端口非法，port: %d，最大端口数量: %d",
                 (int)CST816T_IIC_PORT,
                 (int)I2C_NUM_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    i2c_obj_t iic_obj = iic_init(CST816T_IIC_PORT);
    if (iic_obj.init_flag != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "CST816T 复用 BSP/IIC 初始化失败，port: %d，ret: %s",
                 (int)CST816T_IIC_PORT,
                 esp_err_to_name(iic_obj.init_flag));
        return iic_obj.init_flag;
    }

    *bus = &iic_master[CST816T_IIC_PORT];
    if (((*bus)->init_flag != ESP_OK) || ((*bus)->bus_handle == NULL))
    {
        *bus = NULL;
        ESP_LOGE(TAG, "CST816T I2C 总线状态异常，请确认 BSP/IIC 初始化结果");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/* cst816t_add_iic_device：在当前 I2C 总线上创建 CST816T 设备句柄。
 *
 * 功能：
 *     1. 使用固定 7bit 地址 CST816T_IIC_ADDR，不扫描其它地址；
 *     2. 使用头文件里的通信频率、SCL wait 和超时配置；
 *     3. 如果设备句柄已经存在，直接返回 ESP_OK，避免重复添加。
 *
 * 调用方法：
 *     esp_err_t ret = cst816t_add_iic_device();
 *
 * 返回：
 *     ESP_OK：设备句柄已经可用；
 *     ESP_ERR_INVALID_STATE：I2C 总线尚未准备好；
 *     其它值：ESP-IDF I2C 驱动返回的添加设备错误。
 */
static esp_err_t cst816t_add_iic_device(void)
{
    if (s_cst816t_dev != NULL)
    {
        return ESP_OK;
    }

    if ((s_cst816t_iic == NULL) ||
        (s_cst816t_iic->init_flag != ESP_OK) ||
        (s_cst816t_iic->bus_handle == NULL))
    {
        ESP_LOGE(TAG, "CST816T 添加 I2C 设备失败，I2C 总线尚未准备好");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = CST816T_IIC_ADDR_LEN,
        .device_address = CST816T_IIC_ADDR,
        .scl_speed_hz = CST816T_IIC_FREQ_HZ,
        .scl_wait_us = CST816T_IIC_SCL_WAIT_US,
    };

    esp_err_t ret = i2c_master_bus_add_device(s_cst816t_iic->bus_handle,
                                              &dev_cfg,
                                              &s_cst816t_dev);
    if (ret != ESP_OK)
    {
        s_cst816t_dev = NULL;
        ESP_LOGE(TAG,
                 "CST816T 添加 I2C 设备失败，addr: 0x%02X，ret: %s",
                 (unsigned int)CST816T_IIC_ADDR,
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG,
             "CST816T I2C 设备已添加，addr: 0x%02X，freq: %lu Hz",
             (unsigned int)CST816T_IIC_ADDR,
             (unsigned long)CST816T_IIC_FREQ_HZ);

    return ESP_OK;
}

/* cst816t_read_regs：从 CST816T 连续读取寄存器。
 *
 * 功能：
 *     1. 先向 CST816T 写入寄存器起始地址；
 *     2. 使用 repeated start 读取指定长度的数据；
 *     3. 只负责底层 I2C 读取，不解析坐标。
 *
 * 参数：
 *     reg：起始寄存器地址；
 *     data：读取数据保存缓冲区，不能为 NULL；
 *     len：读取长度，单位字节，不能为 0。
 *
 * 调用方法：
 *     uint8_t data[CST816T_POINT_DATA_LEN] = {0};
 *     esp_err_t ret = cst816t_read_regs(CST816T_POINT_REG_START, data, sizeof(data));
 *
 * 返回：
 *     ESP_OK：读取成功；
 *     ESP_ERR_INVALID_ARG：data 为空或 len 为 0；
 *     ESP_ERR_INVALID_STATE：驱动尚未初始化；
 *     其它值：I2C 通信失败。
 */
static esp_err_t cst816t_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0))
    {
        ESP_LOGE(TAG, "CST816T 读寄存器参数错误，data: %p，len: %u", (void *)data, (unsigned int)len);
        return ESP_ERR_INVALID_ARG;
    }

    if ((!s_cst816t_ready) || (s_cst816t_dev == NULL))
    {
        ESP_LOGE(TAG, "CST816T 尚未初始化，请先调用 cst816t_init()");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_transmit_receive(s_cst816t_dev,
                                                &reg,
                                                sizeof(reg),
                                                data,
                                                len,
                                                CST816T_IIC_TIMEOUT_MS);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "CST816T 读寄存器失败，reg: 0x%02X，len: %u，ret: %s",
                 (unsigned int)reg,
                 (unsigned int)len,
                 esp_err_to_name(ret));
    }

    return ret;
}

/* cst816t_read_reg：从 CST816T 读取单个寄存器。
 *
 * 功能：
 *     cst816t_init() 使用本函数读取 0xA7、0xA8、0xA9 版本信息。
 *
 * 参数：
 *     reg：寄存器地址；
 *     value：输出参数，保存读取结果，不能为 NULL。
 *
 * 调用方法：
 *     uint8_t chip_id = 0;
 *     esp_err_t ret = cst816t_read_reg(CST816T_REG_CHIP_ID, &chip_id);
 */
static esp_err_t cst816t_read_reg(uint8_t reg, uint8_t *value)
{
    return cst816t_read_regs(reg, value, CST816T_SINGLE_REG_DATA_LEN);
}

/* cst816t_print_version：打印 CST816T 版本寄存器。
 *
 * 功能：
 *     按用户要求打印初始化阶段读取到的 0xA7、0xA8、0xA9。
 *
 * 参数：
 *     chip_id：0xA7 寄存器读取值；
 *     proj_id：0xA8 寄存器读取值；
 *     fw_version：0xA9 寄存器读取值。
 */
static void cst816t_print_version(uint8_t chip_id, uint8_t proj_id, uint8_t fw_version)
{
    printf("CST816T VERSION:\n");
    printf("0xA7=0x%02X\n", (unsigned int)chip_id);
    printf("0xA8=0x%02X\n", (unsigned int)proj_id);
    printf("0xA9=0x%02X\n", (unsigned int)fw_version);

    ESP_LOGI(TAG,
             "CST816T 版本信息: 0xA7=0x%02X, 0xA8=0x%02X, 0xA9=0x%02X",
             (unsigned int)chip_id,
             (unsigned int)proj_id,
             (unsigned int)fw_version);
}

/* cst816t_print_raw_regs：打印一次触摸原始寄存器。
 *
 * 功能：
 *     用于确认 GestureID、FingerNum、XH、XL、YH、YL 是否按官方寄存器顺序读取。
 *
 * 参数：
 *     gesture_id：GestureID 原始值；
 *     finger_num：FingerNum 原始值；
 *     xh/xl：XposH/XposL 原始值；
 *     yh/yl：YposH/YposL 原始值。
 */
static void cst816t_print_raw_regs(uint8_t gesture_id,
                                   uint8_t finger_num,
                                   uint8_t xh,
                                   uint8_t xl,
                                   uint8_t yh,
                                   uint8_t yl)
{
#if CST816T_RAW_DEBUG_ENABLE
    printf("TOUCH RAW:\n");
    printf("GestureID=0x%02X\n", (unsigned int)gesture_id);
    printf("FingerNum=%u\n", (unsigned int)finger_num);
    printf("XH=0x%02X\n", (unsigned int)xh);
    printf("XL=0x%02X\n", (unsigned int)xl);
    printf("YH=0x%02X\n", (unsigned int)yh);
    printf("YL=0x%02X\n", (unsigned int)yl);
#else
    (void)gesture_id;
    (void)finger_num;
    (void)xh;
    (void)xl;
    (void)yh;
    (void)yl;
#endif
}

esp_err_t cst816t_init(void)
{
    i2c_obj_t *bus = NULL;
    esp_err_t ret = cst816t_get_iic_bus(&bus);
    if (ret != ESP_OK)
    {
        s_cst816t_iic = NULL;
        s_cst816t_ready = false;
        return ret;
    }

    s_cst816t_iic = bus;

    ret = cst816t_add_iic_device();
    if (ret != ESP_OK)
    {
        s_cst816t_ready = false;
        return ret;
    }

    /* 先置 ready，后续 cst816t_read_reg() 才允许通过统一的读寄存器接口访问芯片。 */
    s_cst816t_ready = true;

    uint8_t chip_id = 0;
    uint8_t proj_id = 0;
    uint8_t fw_version = 0;

    ret = cst816t_read_reg(CST816T_REG_CHIP_ID, &chip_id);
    if (ret != ESP_OK)
    {
        s_cst816t_ready = false;
        return ret;
    }

    ret = cst816t_read_reg(CST816T_REG_PROJ_ID, &proj_id);
    if (ret != ESP_OK)
    {
        s_cst816t_ready = false;
        return ret;
    }

    ret = cst816t_read_reg(CST816T_REG_FW_VERSION, &fw_version);
    if (ret != ESP_OK)
    {
        s_cst816t_ready = false;
        return ret;
    }

    cst816t_print_version(chip_id, proj_id, fw_version);
    ESP_LOGI(TAG, "CST816T 初始化完成，采用 50ms 轮询模式，不使用中断");

    return ESP_OK;
}

bool cst816t_read_point(uint16_t *x, uint16_t *y, bool *pressed)
{
    if ((x == NULL) || (y == NULL) || (pressed == NULL))
    {
        ESP_LOGE(TAG, "cst816t_read_point 参数错误，x/y/pressed 不能为 NULL");
        return false;
    }

    *x = 0;
    *y = 0;
    *pressed = false;

    uint8_t data[CST816T_POINT_DATA_LEN] = {0};
    esp_err_t ret = cst816t_read_regs(CST816T_POINT_REG_START, data, sizeof(data));
    if (ret != ESP_OK)
    {
        return false;
    }

    const uint8_t gesture_id = data[CST816T_DATA_INDEX_GESTURE_ID];
    const uint8_t finger_num = data[CST816T_DATA_INDEX_FINGER_NUM];
    const uint8_t xh = data[CST816T_DATA_INDEX_XPOS_H];
    const uint8_t xl = data[CST816T_DATA_INDEX_XPOS_L];
    const uint8_t yh = data[CST816T_DATA_INDEX_YPOS_H];
    const uint8_t yl = data[CST816T_DATA_INDEX_YPOS_L];

    cst816t_print_raw_regs(gesture_id, finger_num, xh, xl, yh, yl);

    if (finger_num == 0U)
    {
        *pressed = false;
        return true;
    }

    *x = (uint16_t)((((uint16_t)xh & CST816T_COORD_HIGH_MASK)
                    << CST816T_COORD_HIGH_SHIFT) |
                   (uint16_t)xl);
    *y = (uint16_t)((((uint16_t)yh & CST816T_COORD_HIGH_MASK)
                    << CST816T_COORD_HIGH_SHIFT) |
                   (uint16_t)yl);
    *pressed = true;

    printf("TOUCH:\npressed=1\nx=%u\ny=%u\n",
           (unsigned int)(*x),
           (unsigned int)(*y));

    return true;
}

void cst816t_poll_task(void *arg)
{
    (void)arg;

    if (!s_cst816t_ready)
    {
        esp_err_t ret = cst816t_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "CST816T 轮询任务初始化失败，ret: %s", esp_err_to_name(ret));
        }
    }

    while (1)
    {
        uint16_t x = 0;
        uint16_t y = 0;
        bool pressed = false;

        (void)cst816t_read_point(&x, &y, &pressed);

        vTaskDelay(pdMS_TO_TICKS(CST816T_POLL_PERIOD_MS));
    }
}
