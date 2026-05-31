#ifndef __TOUCH_CST816T_H
#define __TOUCH_CST816T_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "iic.h"

/* ============================== CST816T 可调参数区 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存 CST816T 触摸模块后续可能需要调整的参数。
 *     2. 移植到其它项目或更换硬件连线时，优先修改本文件的宏定义，尽量不改 .c 文件。
 *     3. 本模块只依赖 BSP/IIC 提供的 I2C 总线对象，不依赖 BME690、LCD 绘图或其它业务模块。
 */

/* CST816T_IIC_PORT：CST816T 使用的 I2C 控制器编号。
 *
 * 功能：
 *     指定 cst816t_init() 复用哪一路 BSP/IIC 总线访问触摸芯片。
 *
 * 当前配置：
 *     默认复用 IIC_MASTER_PORT，也就是工程已有的 I2C 主机总线。
 *
 * 调用方法：
 *     esp_err_t ret = cst816t_init();    // 内部会调用 iic_init(CST816T_IIC_PORT)
 *
 * 后期调试：
 *     如果触摸芯片迁移到其它 I2C 控制器，只需要优先修改本宏，
 *     同时确认 BSP/IIC 已经为对应控制器配置 SDA/SCL 引脚。
 */
#define CST816T_IIC_PORT                         IIC_MASTER_PORT

/* CST816T_IIC_ADDR：CST816T 的 7bit I2C 固定地址。
 *
 * 功能：
 *     CST816T 官方地址固定为 0x15，本驱动不再扫描地址，也不尝试其它候选地址。
 *
 * 注意：
 *     这里填写 7bit 地址，不需要左移 1 位。
 */
#define CST816T_IIC_ADDR                         0x15U

/* CST816T_IIC_ADDR_LEN：CST816T I2C 设备地址长度。
 *
 * 功能：
 *     配置 ESP-IDF I2C device handle 的 dev_addr_length。
 *
 * 当前配置：
 *     CST816T 使用标准 7bit I2C 地址，因此默认使用 I2C_ADDR_BIT_LEN_7。
 */
#define CST816T_IIC_ADDR_LEN                     I2C_ADDR_BIT_LEN_7

/* CST816T_IIC_FREQ_HZ：CST816T I2C 通信频率，单位 Hz。
 *
 * 功能：
 *     配置 ESP-IDF I2C device handle 的 scl_speed_hz。
 *
 * 当前配置：
 *     默认跟随 BSP/IIC 的 IIC_MASTER_FREQ_HZ，避免同一条总线出现两套不一致的频率配置。
 */
#define CST816T_IIC_FREQ_HZ                      IIC_MASTER_FREQ_HZ

/* CST816T_IIC_TIMEOUT_MS：CST816T 单次 I2C 读写超时时间，单位 ms。
 *
 * 功能：
 *     传给 i2c_master_transmit_receive()，防止总线异常时任务永久阻塞。
 *
 * 后期调试：
 *     如果触摸排线较长、总线波形较差或偶发超时，可适当增大该值。
 */
#define CST816T_IIC_TIMEOUT_MS                   IIC_TIMEOUT_MS

/* CST816T_IIC_SCL_WAIT_US：I2C SCL 等待超时时间，单位 us。
 *
 * 功能：
 *     传给 ESP-IDF I2C device 配置中的 scl_wait_us。
 *
 * 当前配置：
 *     默认复用 BSP/IIC 的 IIC_SCL_WAIT_US，0 表示使用 ESP-IDF 默认值。
 */
#define CST816T_IIC_SCL_WAIT_US                  IIC_SCL_WAIT_US

/* CST816T_REG_GESTURE_ID：手势 ID 寄存器。
 *
 * 功能：
 *     官方坐标数据区的第 1 个寄存器，用于读取芯片识别到的手势编号。
 */
#define CST816T_REG_GESTURE_ID                   0x01U

/* CST816T_REG_FINGER_NUM：触摸点数量寄存器。
 *
 * 功能：
 *     官方坐标数据区的第 2 个寄存器。
 *     FingerNum == 0 时表示当前没有按下；
 *     FingerNum > 0 时表示当前存在触摸点，驱动输出 pressed=true。
 */
#define CST816T_REG_FINGER_NUM                   0x02U

/* CST816T_REG_XPOS_H / CST816T_REG_XPOS_L：X 坐标高/低字节寄存器。
 *
 * 功能：
 *     按 CST816T 官方寄存器定义读取 XposH 和 XposL。
 *     坐标解析公式：
 *         x = ((XposH & CST816T_COORD_HIGH_MASK) << CST816T_COORD_HIGH_SHIFT) | XposL
 */
#define CST816T_REG_XPOS_H                       0x03U
#define CST816T_REG_XPOS_L                       0x04U

/* CST816T_REG_YPOS_H / CST816T_REG_YPOS_L：Y 坐标高/低字节寄存器。
 *
 * 功能：
 *     按 CST816T 官方寄存器定义读取 YposH 和 YposL。
 *     坐标解析公式：
 *         y = ((YposH & CST816T_COORD_HIGH_MASK) << CST816T_COORD_HIGH_SHIFT) | YposL
 */
#define CST816T_REG_YPOS_H                       0x05U
#define CST816T_REG_YPOS_L                       0x06U

/* CST816T_POINT_REG_START：触摸点连续读取起始寄存器。
 *
 * 功能：
 *     cst816t_read_point() 从 GestureID 开始一次性读取 6 个官方寄存器：
 *         GestureID, FingerNum, XposH, XposL, YposH, YposL
 */
#define CST816T_POINT_REG_START                  CST816T_REG_GESTURE_ID

/* CST816T_POINT_DATA_LEN：一次读取触摸点数据的长度，单位字节。
 *
 * 功能：
 *     覆盖 0x01~0x06 共 6 个寄存器，避免旧代码从错误起始地址猜测坐标。
 */
#define CST816T_POINT_DATA_LEN                   6U

/* CST816T_SINGLE_REG_DATA_LEN：读取单个寄存器时的数据长度，单位字节。
 *
 * 功能：
 *     cst816t_init() 读取 0xA7、0xA8、0xA9 版本寄存器时使用。
 */
#define CST816T_SINGLE_REG_DATA_LEN              1U

/* CST816T_DATA_INDEX_*：连续读取缓冲区内各寄存器的索引。
 *
 * 功能：
 *     cst816t_read_point() 通过这些索引解析 data[]，避免在代码里写裸数字。
 */
#define CST816T_DATA_INDEX_GESTURE_ID            0U
#define CST816T_DATA_INDEX_FINGER_NUM            1U
#define CST816T_DATA_INDEX_XPOS_H                2U
#define CST816T_DATA_INDEX_XPOS_L                3U
#define CST816T_DATA_INDEX_YPOS_H                4U
#define CST816T_DATA_INDEX_YPOS_L                5U

/* CST816T_COORD_HIGH_MASK：坐标高字节有效位掩码。
 *
 * 功能：
 *     CST816T 坐标高字节低 4bit 参与 12bit 坐标计算，高 4bit 可能包含事件或保留信息。
 */
#define CST816T_COORD_HIGH_MASK                  0x0FU

/* CST816T_COORD_HIGH_SHIFT：坐标高 4bit 左移位数。
 *
 * 功能：
 *     配合 CST816T_COORD_HIGH_MASK 还原 12bit 坐标值。
 */
#define CST816T_COORD_HIGH_SHIFT                 8U

/* CST816T_REG_CHIP_ID / CST816T_REG_PROJ_ID / CST816T_REG_FW_VERSION：版本信息寄存器。
 *
 * 功能：
 *     cst816t_init() 初始化时读取 0xA7、0xA8、0xA9 并打印芯片版本信息。
 */
#define CST816T_REG_CHIP_ID                      0xA7U
#define CST816T_REG_PROJ_ID                      0xA8U
#define CST816T_REG_FW_VERSION                   0xA9U

/* CST816T_RAW_DEBUG_ENABLE：是否打印原始触摸寄存器。
 *
 * 功能：
 *     1：每次 cst816t_read_point() 成功读取后打印 GestureID、FingerNum、XH、XL、YH、YL；
 *     0：关闭原始寄存器打印，减少串口日志量。
 *
 * 当前要求：
 *     为了确认官方寄存器解析是否正确，默认开启。
 */
#define CST816T_RAW_DEBUG_ENABLE                 1

/* CST816T_POLL_PERIOD_MS：触摸轮询周期，单位 ms。
 *
 * 功能：
 *     cst816t_poll_task() 每隔该时间调用一次 cst816t_read_point()。
 *
 * 当前要求：
 *     暂时采用轮询模式，不使用中断，轮询周期固定为 50ms。
 */
#define CST816T_POLL_PERIOD_MS                   50U

/* CST816T_POLL_TASK_NAME：触摸轮询任务名称。
 *
 * 功能：
 *     main.c 创建 FreeRTOS 任务时使用，便于调试器和日志识别任务。
 */
#define CST816T_POLL_TASK_NAME                   "cst816t_poll_task"

/* CST816T_POLL_TASK_STACK_SIZE：触摸轮询任务栈大小，单位 Byte。
 *
 * 功能：
 *     任务只做 I2C 读取和串口打印，默认 4096 Byte 预留较充足。
 */
#define CST816T_POLL_TASK_STACK_SIZE             4096U

/* CST816T_POLL_TASK_PRIORITY：触摸轮询任务优先级。
 *
 * 功能：
 *     触摸轮询属于低频输入任务，默认优先级不高，避免影响 BME690 和 LCD。
 */
#define CST816T_POLL_TASK_PRIORITY               5U

/* CST816T_LOG_TAG：CST816T 日志 TAG。
 *
 * 功能：
 *     用于 ESP_LOGI/ESP_LOGE 的模块标识，方便串口日志过滤。
 */
#define CST816T_LOG_TAG                          "CST816T"

/* ============================== 对外函数声明 ============================== */

/* cst816t_init：初始化 CST816T 触摸驱动。
 *
 * 功能：
 *     1. 复用 BSP/IIC 初始化 I2C 总线；
 *     2. 在固定地址 0x15 上创建 CST816T I2C device handle；
 *     3. 读取 0xA7、0xA8、0xA9 并打印芯片版本信息；
 *     4. 不使用中断，不扫描地址，不写入会影响触摸/LCD/BME690 的其它配置。
 *
 * 调用方法：
 *     esp_err_t ret = cst816t_init();
 *     if (ret != ESP_OK) {
 *         // 根据 esp_err_to_name(ret) 排查 I2C 总线或触摸芯片连接
 *     }
 *
 * 返回：
 *     ESP_OK：初始化成功；
 *     ESP_ERR_INVALID_ARG：I2C 端口配置或参数非法；
 *     其它值：I2C 初始化、添加设备或读取版本寄存器失败。
 */
esp_err_t cst816t_init(void);

/* cst816t_read_point：读取一次 CST816T 当前触摸状态和坐标。
 *
 * 功能：
 *     1. 按官方寄存器顺序读取 GestureID、FingerNum、XposH、XposL、YposH、YposL；
 *     2. FingerNum == 0 时输出 pressed=false，并把 x/y 清零；
 *     3. FingerNum > 0 时输出 pressed=true，解析 x/y，并打印：
 *            TOUCH:
 *            pressed=1
 *            x=%u
 *            y=%u
 *     4. 每次成功读取都会按 CST816T_RAW_DEBUG_ENABLE 配置打印原始寄存器。
 *
 * 参数：
 *     x：输出参数，保存解析后的 X 坐标，不能为 NULL；
 *     y：输出参数，保存解析后的 Y 坐标，不能为 NULL；
 *     pressed：输出参数，true 表示 FingerNum > 0，不能为 NULL。
 *
 * 调用方法：
 *     uint16_t x = 0;
 *     uint16_t y = 0;
 *     bool pressed = false;
 *
 *     if (cst816t_read_point(&x, &y, &pressed) && pressed) {
 *         // 此时 x/y 为最新触摸坐标
 *     }
 *
 * 返回：
 *     true：寄存器读取成功，pressed/x/y 已更新；
 *     false：参数错误、未初始化或 I2C 读取失败。
 */
bool cst816t_read_point(uint16_t *x, uint16_t *y, bool *pressed);

/* cst816t_poll_task：CST816T 轮询读取任务。
 *
 * 功能：
 *     1. 暂时采用轮询模式，不使用中断；
 *     2. 每隔 CST816T_POLL_PERIOD_MS 调用一次 cst816t_read_point()；
 *     3. 只通过串口打印触摸状态，不调用 LCD 绘图接口，因此不会影响 LCD 模块。
 *
 * 调用方法：
 *     xTaskCreate(cst816t_poll_task,
 *                 CST816T_POLL_TASK_NAME,
 *                 CST816T_POLL_TASK_STACK_SIZE,
 *                 NULL,
 *                 CST816T_POLL_TASK_PRIORITY,
 *                 NULL);
 *
 * 注意：
 *     建议先调用 cst816t_init()，初始化成功后再创建本任务。
 *     如果外部直接创建任务，任务内部也会尝试初始化 CST816T。
 */
void cst816t_poll_task(void *arg);

#endif
