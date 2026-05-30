#include "iic.h"

#include "esp_log.h"

static const char *TAG = "IIC";

i2c_obj_t iic_master[I2C_NUM_MAX];            /* I2C 主机总线对象数组，按 I2C 端口号保存 */

/* iic_get_gpio：根据 I2C 端口号获取当前工程配置的 SDA/SCL 引脚。
 *
 * 当前 board_bme690 工程只配置 I2C0。如后续硬件新增 I2C1，只需要在 iic.h 中增加
 * 对应 GPIO 宏，并在本函数中增加 case 分支，不需要改动业务层代码。
 */
static esp_err_t iic_get_gpio(i2c_port_num_t port, gpio_num_t *sda, gpio_num_t *scl)
{
    if ((sda == NULL) || (scl == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    switch (port)
    {
        case I2C_NUM_0:
            *sda = HPIIC0_SDA_GPIO_PIN;
            *scl = HPIIC0_SCL_GPIO_PIN;
            return ESP_OK;

        default:
            ESP_LOGI(TAG, "当前未配置 I2C%d 的 SDA/SCL 引脚", (int)port);
            return ESP_ERR_NOT_SUPPORTED;
    }
}

/* iic_check_ready：检查 I2C 总线对象是否已经初始化完成。
 *
 * 所有读写/扫描接口都会先调用本函数，避免在未初始化时访问 ESP-IDF 驱动句柄。
 */
static esp_err_t iic_check_ready(i2c_obj_t *self)
{
    if (self == NULL)
    {
        ESP_LOGI(TAG, "I2C 对象为空，请先调用 iic_init()");
        return ESP_ERR_INVALID_ARG;
    }

    if ((self->init_flag != ESP_OK) || (self->bus_handle == NULL))
    {
        ESP_LOGI(TAG, "I2C%d 未初始化，请先调用 iic_init()", (int)self->port);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

/* iic_check_addr：检查 I2C 从设备地址是否合法。
 *
 * 本模块统一使用 7bit 地址，例如 BME690 常见地址为 0x76 或 0x77，不需要左移 1 位。
 */
static esp_err_t iic_check_addr(uint16_t addr)
{
    if (addr > IIC_ADDR_7BIT_MAX)
    {
        ESP_LOGI(TAG, "I2C 地址 0x%02X 非法，请传入 7bit 地址", (unsigned int)addr);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* iic_add_device：在当前 I2C 总线上临时添加一个从设备句柄。
 *
 * ESP-IDF v5 新版 I2C API 要先通过 i2c_master_bus_add_device() 创建设备句柄，
 * 再使用 i2c_master_transmit()/i2c_master_receive() 完成读写。
 * 为了让 BSP 模块保持独立，本模块在每次读写时临时创建设备句柄，读写结束后释放。
 */
static esp_err_t iic_add_device(i2c_obj_t *self, uint16_t addr, i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t ret;

    if (dev_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ret = iic_check_ready(self);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = iic_check_addr(addr);
    if (ret != ESP_OK)
    {
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = IIC_DEVICE_ADDR_LEN,
        .device_address = addr,
        .scl_speed_hz = IIC_MASTER_FREQ_HZ,
        .scl_wait_us = IIC_SCL_WAIT_US,
    };

    ret = i2c_master_bus_add_device(self->bus_handle, &dev_cfg, dev_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGI(TAG, "添加 I2C 设备 0x%02X 失败，ret: %d", (unsigned int)addr, ret);
    }

    return ret;
}

/* iic_remove_device：释放临时 I2C 从设备句柄。
 *
 * 该函数只在 BSP 内部使用，保证每次 iic_write()/iic_read() 结束后不会残留设备句柄。
 */
static void iic_remove_device(i2c_master_dev_handle_t dev_handle)
{
    if (dev_handle == NULL)
    {
        return;
    }

    esp_err_t ret = i2c_master_bus_rm_device(dev_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGI(TAG, "移除 I2C 设备句柄失败，ret: %d", ret);
    }
}

i2c_obj_t iic_init(uint8_t iic_port)
{
    i2c_obj_t obj = {
        .port = iic_port,
        .scl = HPIIC0_SCL_GPIO_PIN,
        .sda = HPIIC0_SDA_GPIO_PIN,
        .init_flag = ESP_FAIL,
        .bus_handle = NULL,
    };

    if (iic_port >= I2C_NUM_MAX)
    {
        ESP_LOGI(TAG, "I2C%d 端口非法，最大端口数量: %d", (int)iic_port, (int)I2C_NUM_MAX);
        obj.init_flag = ESP_ERR_INVALID_ARG;
        return obj;
    }

    i2c_obj_t *self = &iic_master[iic_port];

    if ((self->init_flag == ESP_OK) && (self->bus_handle != NULL))
    {
        ESP_LOGI(TAG, "I2C%d 已初始化，SDA: %d, SCL: %d, freq: %d Hz",
                 (int)self->port, (int)self->sda, (int)self->scl, IIC_MASTER_FREQ_HZ);
        return *self;
    }

    self->port = iic_port;
    self->init_flag = ESP_FAIL;
    self->bus_handle = NULL;

    esp_err_t ret = iic_get_gpio(self->port, &self->sda, &self->scl);
    if (ret != ESP_OK)
    {
        self->init_flag = ret;
        return *self;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = self->port,
        .sda_io_num = self->sda,
        .scl_io_num = self->scl,
        .glitch_ignore_cnt = IIC_GLITCH_IGNORE_CNT,
        .intr_priority = IIC_MASTER_INTR_PRIORITY,
        .trans_queue_depth = IIC_MASTER_TRANS_QUEUE_DEPTH,
        .flags.enable_internal_pullup = IIC_ENABLE_INTERNAL_PULLUP,
    };

    ESP_LOGI(TAG, "开始初始化 I2C%d 主机，SDA: %d, SCL: %d, freq: %d Hz",
             (int)self->port, (int)self->sda, (int)self->scl, IIC_MASTER_FREQ_HZ);

    ret = i2c_new_master_bus(&bus_cfg, &self->bus_handle);
    self->init_flag = ret;

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "I2C%d 主机初始化成功", (int)self->port);
    }
    else
    {
        self->bus_handle = NULL;
        ESP_LOGI(TAG, "I2C%d 主机初始化失败，ret: %d", (int)self->port, ret);
    }

    return *self;
}

esp_err_t iic_write(i2c_obj_t *self, uint16_t addr, const uint8_t *write_buf, size_t write_len)
{
    if ((write_buf == NULL) || (write_len == 0))
    {
        ESP_LOGI(TAG, "I2C 写入参数错误，write_buf: %p, write_len: %d", (const void *)write_buf, (int)write_len);
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t ret = iic_add_device(self, addr, &dev_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = i2c_master_transmit(dev_handle, write_buf, write_len, IIC_TIMEOUT_MS);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "I2C 写入成功，addr: 0x%02X, len: %d", (unsigned int)addr, (int)write_len);
    }
    else
    {
        ESP_LOGI(TAG, "I2C 写入失败，addr: 0x%02X, len: %d, ret: %d",
                 (unsigned int)addr, (int)write_len, ret);
    }

    iic_remove_device(dev_handle);
    return ret;
}

esp_err_t iic_read(i2c_obj_t *self,
                   uint16_t addr,
                   const uint8_t *write_buf,
                   size_t write_len,
                   uint8_t *read_buf,
                   size_t read_len)
{
    if ((read_buf == NULL) || (read_len == 0))
    {
        ESP_LOGI(TAG, "I2C 读取参数错误，read_buf: %p, read_len: %d", (void *)read_buf, (int)read_len);
        return ESP_ERR_INVALID_ARG;
    }

    if ((write_len > 0) && (write_buf == NULL))
    {
        ESP_LOGI(TAG, "I2C 读取前置写参数错误，write_len 不为 0 时 write_buf 不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t ret = iic_add_device(self, addr, &dev_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (write_len > 0)
    {
        /* 常见寄存器读取时序：
         * 1. 主机先写入寄存器地址；
         * 2. 不释放总线，自动产生 repeated start；
         * 3. 主机读取寄存器数据。
         */
        ret = i2c_master_transmit_receive(dev_handle,
                                          write_buf,
                                          write_len,
                                          read_buf,
                                          read_len,
                                          IIC_TIMEOUT_MS);
    }
    else
    {
        /* 纯读取时序：适用于不需要先发送寄存器地址的 I2C 从设备。 */
        ret = i2c_master_receive(dev_handle, read_buf, read_len, IIC_TIMEOUT_MS);
    }

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "I2C 读取成功，addr: 0x%02X, write_len: %d, read_len: %d",
                 (unsigned int)addr, (int)write_len, (int)read_len);
    }
    else
    {
        ESP_LOGI(TAG, "I2C 读取失败，addr: 0x%02X, write_len: %d, read_len: %d, ret: %d",
                 (unsigned int)addr, (int)write_len, (int)read_len, ret);
    }

    iic_remove_device(dev_handle);
    return ret;
}

esp_err_t iic_scan(i2c_obj_t *self)
{
    uint8_t found_num = 0;

    esp_err_t ret = iic_check_ready(self);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_LOGI(TAG,
             "开始扫描 I2C%d，完整 7bit 地址范围: 0x%02X - 0x%02X，单地址超时: %d ms",
             (int)self->port,
             (unsigned int)IIC_SCAN_ADDR_START,
             (unsigned int)IIC_SCAN_ADDR_END,
             IIC_SCAN_TIMEOUT_MS);

    /* 使用 ESP-IDF v5 I2C 主机 API 探测全部 7bit 地址。
     * 调用方法：
     *     iic_scan(&iic_master[IIC_MASTER_PORT]);
     *
     * 说明：
     *     i2c_master_probe() 会在指定地址上发送探测时序，返回 ESP_OK 表示该地址有设备响应。
     */
    for (uint16_t addr = IIC_SCAN_ADDR_START; addr <= IIC_SCAN_ADDR_END; addr++)
    {
        ret = i2c_master_probe(self->bus_handle, addr, IIC_SCAN_TIMEOUT_MS);
        if (ret == ESP_OK)
        {
            found_num++;
            ESP_LOGI(TAG, "发现 I2C 设备，7bit 地址: 0x%02X", (unsigned int)addr);
        }
    }

    if (found_num == 0)
    {
        ESP_LOGI(TAG, "I2C%d 扫描完成，未发现 I2C 设备", (int)self->port);
    }
    else
    {
        ESP_LOGI(TAG, "I2C%d 扫描完成，共发现 %u 个 I2C 设备",
                 (int)self->port,
                 (unsigned int)found_num);
    }

    return ESP_OK;
}
