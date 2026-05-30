#ifndef __IIC_H
#define __IIC_H

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

/* ============================== I2C 可调参数区 ============================== */

/* IIC_MASTER_PORT：当前 BSP 默认使用的 I2C 控制器编号。
 * 调用方法：iic_init(IIC_MASTER_PORT);
 * 后续如果硬件改到其它 I2C 控制器，只需要优先修改这里或调用 iic_init() 时传入新端口。
 */
#define IIC_MASTER_PORT                    I2C_NUM_0

/* HPIIC0_SDA_GPIO_PIN：I2C0 的 SDA 数据线 GPIO。
 * BME690 的 SDA 引脚需要连接到该 GPIO；换板或飞线调试时修改这里。
 */
#define HPIIC0_SDA_GPIO_PIN                GPIO_NUM_2

/* HPIIC0_SCL_GPIO_PIN：I2C0 的 SCL 时钟线 GPIO。
 * BME690 的 SCL 引脚需要连接到该 GPIO；换板或飞线调试时修改这里。
 */
#define HPIIC0_SCL_GPIO_PIN                GPIO_NUM_3

/* IIC_MASTER_FREQ_HZ：I2C 总线通信频率，单位 Hz。
 * BME690 支持标准模式/快速模式，常用 100000 或 400000；总线较长时建议先降到 100000。
 */
#define IIC_MASTER_FREQ_HZ                 400000

/* IIC_FREQ：兼容旧工程命名，实际等同于 IIC_MASTER_FREQ_HZ。 */
#define IIC_FREQ                           IIC_MASTER_FREQ_HZ

/* IIC_GLITCH_IGNORE_CNT：I2C 硬件毛刺过滤计数。
 * 数值越大，对短毛刺越不敏感；一般保持 7 即可。
 */
#define IIC_GLITCH_IGNORE_CNT              7

/* IIC_ENABLE_INTERNAL_PULLUP：是否开启 ESP32 内部上拉。
 * 1：开启内部上拉；0：关闭内部上拉。
 * 注意：I2C 总线通常仍建议外接 4.7K 左右上拉电阻，内部上拉只适合短线调试。
 */
#define IIC_ENABLE_INTERNAL_PULLUP         1

/* IIC_MASTER_INTR_PRIORITY：I2C 中断优先级。
 * 0 表示使用 ESP-IDF 默认优先级；通常不需要修改。
 */
#define IIC_MASTER_INTR_PRIORITY           0

/* IIC_MASTER_TRANS_QUEUE_DEPTH：异步传输队列深度。
 * 当前模块使用同步阻塞读写接口，保持 0 即可。
 */
#define IIC_MASTER_TRANS_QUEUE_DEPTH       0

/* IIC_DEVICE_ADDR_LEN：I2C 设备地址长度。
 * BME690 使用 7bit 地址，因此默认 I2C_ADDR_BIT_LEN_7。
 */
#define IIC_DEVICE_ADDR_LEN                I2C_ADDR_BIT_LEN_7

/* IIC_SCL_WAIT_US：SCL 等待超时时间，单位 us。
 * 0 表示使用 ESP-IDF 默认值；只有遇到特殊慢速从机时才需要调整。
 */
#define IIC_SCL_WAIT_US                    0

/* IIC_TIMEOUT_MS：普通 I2C 读写超时时间，单位 ms。
 * BME690 寄存器读写数据量很小，1000ms 已经足够宽松。
 */
#define IIC_TIMEOUT_MS                     1000

/* IIC_ADDR_7BIT_MIN：7bit I2C 地址最小值。
 * 功能：
 *     用于 I2C Scan 的完整地址范围起点。
 * 调用方法：
 *     iic_scan(&iic_master[IIC_MASTER_PORT]);     // 内部从 IIC_ADDR_7BIT_MIN 开始扫描
 */
#define IIC_ADDR_7BIT_MIN                  0x00

/* IIC_ADDR_7BIT_MAX：7bit I2C 地址最大值。
 * 用于接口入参检查，防止误把 8bit 地址或非法地址传入驱动。
 */
#define IIC_ADDR_7BIT_MAX                  0x7F

/* IIC_SCAN_TIMEOUT_MS：扫描单个地址时的探测超时时间，单位 ms。
 * 功能：
 *     iic_scan() 调用 ESP-IDF v5 的 i2c_master_probe() 探测单个地址时使用该超时时间。
 * 调试说明：
 *     数值越大，对慢速从设备越宽容，但完整扫描 0x00~0x7F 的时间也会变长。
 */
#define IIC_SCAN_TIMEOUT_MS                50

/* IIC_SCAN_ADDR_START：I2C Scan 起始地址。
 * 功能：
 *     当前按要求扫描全部 7bit I2C 地址，因此默认从 0x00 开始。
 * 调用方法：
 *     iic_scan(&iic_master[IIC_MASTER_PORT]);
 */
#define IIC_SCAN_ADDR_START                IIC_ADDR_7BIT_MIN

/* IIC_SCAN_ADDR_END：I2C Scan 结束地址。
 * 功能：
 *     当前按要求扫描全部 7bit I2C 地址，因此默认扫描到 0x7F。
 * 调用方法：
 *     iic_scan(&iic_master[IIC_MASTER_PORT]);
 */
#define IIC_SCAN_ADDR_END                  IIC_ADDR_7BIT_MAX

/* ============================== 数据结构定义 ============================== */

typedef struct _i2c_obj_t
{
    i2c_port_num_t port;                       /* I2C 控制器编号，例如 I2C_NUM_0 */
    gpio_num_t scl;                            /* 当前 I2C 总线使用的 SCL GPIO */
    gpio_num_t sda;                            /* 当前 I2C 总线使用的 SDA GPIO */
    esp_err_t init_flag;                       /* 初始化结果，ESP_OK 表示初始化成功 */
    i2c_master_bus_handle_t bus_handle;        /* ESP-IDF v5 I2C 主机总线句柄 */
} i2c_obj_t;

typedef struct _i2c_buf_t
{
    size_t len;                                /* 缓冲区长度，单位字节 */
    uint8_t *buf;                              /* 缓冲区首地址 */
} i2c_buf_t;

/* iic_master：I2C BSP 模块维护的总线对象数组。
 * 调用方法：
 *     i2c_obj_t iic0 = iic_init(IIC_MASTER_PORT);
 *     iic_write(&iic_master[IIC_MASTER_PORT], 0x76, data, len);
 */
extern i2c_obj_t iic_master[I2C_NUM_MAX];

/* ============================== 函数声明 ============================== */

/* iic_init：初始化 I2C 主机总线。
 *
 * 参数：
 *     iic_port：I2C 控制器编号，当前默认使用 IIC_MASTER_PORT。
 *
 * 返回：
 *     i2c_obj_t：I2C 总线对象，init_flag 为 ESP_OK 表示初始化成功。
 *
 * 调用方法：
 *     i2c_obj_t iic0 = iic_init(IIC_MASTER_PORT);
 *     if (iic0.init_flag != ESP_OK) {
 *         // 初始化失败处理
 *     }
 */
i2c_obj_t iic_init(uint8_t iic_port);

/* iic_write：向指定 I2C 从设备写入数据。
 *
 * 参数：
 *     self：iic_init() 初始化后的 I2C 总线对象指针。
 *     addr：7bit 从设备地址，例如 BME690 常见地址 0x76 或 0x77。
 *     write_buf：待写入数据缓冲区。
 *     write_len：待写入数据长度，单位字节。
 *
 * 返回：
 *     ESP_OK 表示写入成功，其它值表示失败。
 *
 * 调用方法：
 *     uint8_t cmd[] = {0xD0};                 // 例如写入寄存器地址
 *     esp_err_t ret = iic_write(&iic_master[IIC_MASTER_PORT], 0x76, cmd, sizeof(cmd));
 */
esp_err_t iic_write(i2c_obj_t *self, uint16_t addr, const uint8_t *write_buf, size_t write_len);

/* iic_read：从指定 I2C 从设备读取数据。
 *
 * 参数：
 *     self：iic_init() 初始化后的 I2C 总线对象指针。
 *     addr：7bit 从设备地址，例如 BME690 常见地址 0x76 或 0x77。
 *     write_buf：读之前需要先写入的数据，常用于发送寄存器地址；不需要时传 NULL。
 *     write_len：读之前需要先写入的数据长度；不需要时传 0。
 *     read_buf：读取数据保存缓冲区。
 *     read_len：需要读取的数据长度，单位字节。
 *
 * 返回：
 *     ESP_OK 表示读取成功，其它值表示失败。
 *
 * 调用方法：
 *     uint8_t reg = 0xD0;                     // BME690 chip id 寄存器
 *     uint8_t id = 0;
 *     esp_err_t ret = iic_read(&iic_master[IIC_MASTER_PORT], 0x76, &reg, 1, &id, 1);
 *
 *     // 如果从设备不需要先写寄存器地址，可这样调用：
 *     esp_err_t ret2 = iic_read(&iic_master[IIC_MASTER_PORT], 0x76, NULL, 0, buf, len);
 */
esp_err_t iic_read(i2c_obj_t *self,
                   uint16_t addr,
                   const uint8_t *write_buf,
                   size_t write_len,
                   uint8_t *read_buf,
                   size_t read_len);

/* iic_scan：扫描当前 I2C 总线上的全部 7bit 从设备地址。
 *
 * 参数：
 *     self：iic_init() 初始化后的 I2C 总线对象指针。
 *
 * 返回：
 *     ESP_OK 表示扫描流程执行完成，其它值表示总线未初始化或参数错误。
 *
 * 调用方法：
 *     iic_init(IIC_MASTER_PORT);
 *     iic_scan(&iic_master[IIC_MASTER_PORT]);
 *
 * 功能：
 *     使用 ESP-IDF v5 的 i2c_master_probe() 从 IIC_SCAN_ADDR_START 扫描到
 *     IIC_SCAN_ADDR_END，并通过 ESP_LOGI 打印发现的设备地址。
 */
esp_err_t iic_scan(i2c_obj_t *self);

#endif
