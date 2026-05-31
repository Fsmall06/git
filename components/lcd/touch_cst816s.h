#ifndef __TOUCH_CST816S_H
#define __TOUCH_CST816S_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "iic.h"

/* ============================== CST816S 可调参数区 ==============================
 *
 * 本文件集中保存触摸模块后续可能需要调整的参数。
 * 移植到其它板子时，优先检查和修改本区域宏定义，尽量不要直接改 .c 文件。
 */

/* TOUCH_CST816S_IIC_PORT：CST816S 使用的 I2C 控制器编号。
 *
 * 功能：
 *     指定 touch_init() 初始化并复用哪一路 BSP/IIC 总线。
 *
 * 当前配置：
 *     默认复用工程已有的 IIC_MASTER_PORT，即当前 BME690 所在 I2C 总线。
 *
 * 调用方法：
 *     esp_err_t ret = touch_init();    // 内部会调用 iic_init(TOUCH_CST816S_IIC_PORT)
 *
 * 后期调试：
 *     如果触摸芯片迁移到其它 I2C 控制器，只需要优先修改本宏，
 *     同时确认 BSP/IIC 已经配置对应控制器的 SDA/SCL 引脚。
 */
#define TOUCH_CST816S_IIC_PORT                  IIC_MASTER_PORT

/* TOUCH_CST816S_IIC_ADDR：CST816S 的 7bit I2C 固定地址。
 *
 * 功能：
 *     touch_read() 使用该地址读取触摸坐标寄存器。
 *
 * 当前配置：
 *     已确认触摸芯片 I2C 地址为 0x15。
 *
 * 注意：
 *     本驱动不再做地址扫描，也不再尝试其它候选地址。
 */
#define TOUCH_CST816S_IIC_ADDR                  0x15U

/* TOUCH_CST816S_REG_FINGER_NUM：触摸点数量寄存器。
 *
 * 功能：
 *     touch_read() 读取该寄存器判断当前是否按下。
 *     当 FingerNum > 0 时，pressed 输出 true；否则输出 false。
 */
#define TOUCH_CST816S_REG_FINGER_NUM           0x03U

/* TOUCH_CST816S_REG_XPOS_H / TOUCH_CST816S_REG_XPOS_L：X 坐标高/低字节寄存器。
 *
 * 功能：
 *     X 坐标解析公式固定为：
 *         x = ((xh & 0x0F) << 8) | xl
 *
 * 调用方法：
 *     uint16_t x = 0;
 *     uint16_t y = 0;
 *     bool pressed = false;
 *     touch_read(&x, &y, &pressed);
 */
#define TOUCH_CST816S_REG_XPOS_H               0x04U
#define TOUCH_CST816S_REG_XPOS_L               0x05U

/* TOUCH_CST816S_REG_YPOS_H / TOUCH_CST816S_REG_YPOS_L：Y 坐标高/低字节寄存器。
 *
 * 功能：
 *     Y 坐标解析公式固定为：
 *         y = ((yh & 0x0F) << 8) | yl
 */
#define TOUCH_CST816S_REG_YPOS_H               0x06U
#define TOUCH_CST816S_REG_YPOS_L               0x07U

/* TOUCH_CST816S_POINT_DATA_LEN：一次读取触摸数据的连续寄存器长度。
 *
 * 功能：
 *     touch_read() 从 0x03 开始连续读取 0x03~0x07 共 5 字节：
 *         0x03 FingerNum
 *         0x04 XposH
 *         0x05 XposL
 *         0x06 YposH
 *         0x07 YposL
 *
 * 后期调试：
 *     如果后续需要扩展手势、事件或更多寄存器，可在 .c 中同步扩展缓冲区解析逻辑。
 */
#define TOUCH_CST816S_POINT_DATA_LEN           5U

/* TOUCH_CST816S_COORD_HIGH_MASK：坐标高字节有效位掩码。
 *
 * 功能：
 *     CST816S 坐标高字节仅低 4bit 参与坐标计算，高 4bit 可能包含事件/标志位。
 */
#define TOUCH_CST816S_COORD_HIGH_MASK          0x0FU

/* TOUCH_CST816S_COORD_HIGH_SHIFT：坐标高 4bit 左移位数。
 *
 * 功能：
 *     配合 TOUCH_CST816S_COORD_HIGH_MASK 还原 12bit 坐标值。
 */
#define TOUCH_CST816S_COORD_HIGH_SHIFT         8U

/* TOUCH_CST816S_TEST_PERIOD_MS：触摸测试任务读取周期，单位 ms。
 *
 * 功能：
 *     touch_test_task() 每隔该时间调用一次 touch_read()。
 *
 * 当前要求：
 *     周期固定为 100ms。
 */
#define TOUCH_CST816S_TEST_PERIOD_MS           100U

/* TOUCH_CST816S_TEST_TASK_NAME：触摸测试任务名称。
 *
 * 功能：
 *     main.c 创建 FreeRTOS 任务时使用，便于调试器和日志识别任务。
 */
#define TOUCH_CST816S_TEST_TASK_NAME           "touch_test_task"

/* TOUCH_CST816S_TEST_TASK_STACK_SIZE：触摸测试任务栈大小，单位 Byte。
 *
 * 功能：
 *     任务只做 I2C 读取和 printf 输出，默认 4096 Byte 预留较充足。
 *     如果后续增加滤波、手势识别或更多日志，可根据栈水位再调整。
 */
#define TOUCH_CST816S_TEST_TASK_STACK_SIZE     4096U

/* TOUCH_CST816S_TEST_TASK_PRIORITY：触摸测试任务优先级。
 *
 * 功能：
 *     触摸测试属于低频调试任务，默认优先级不高，避免影响传感器和显示任务。
 */
#define TOUCH_CST816S_TEST_TASK_PRIORITY       5U

/* ============================== 函数声明 ============================== */

/* touch_init：初始化 CST816S 触摸驱动。
 *
 * 功能：
 *     1. 复用 BSP/IIC 模块初始化 I2C 总线；
 *     2. 保存 I2C 总线对象，供 touch_read() 后续读取坐标寄存器；
 *     3. 不做地址扫描，不读取 Chip ID，不做型号探测。
 *
 * 调用方法：
 *     esp_err_t ret = touch_init();
 *     if (ret != ESP_OK) {
 *         // 根据 esp_err_to_name(ret) 排查 I2C 初始化或总线状态
 *     }
 *
 * 返回：
 *     ESP_OK：初始化成功；
 *     ESP_ERR_INVALID_ARG：I2C 端口配置非法；
 *     其它值：BSP/IIC 初始化或状态检查失败。
 */
esp_err_t touch_init(void);

/* touch_read：读取 CST816S 当前触摸状态和坐标。
 *
 * 功能：
 *     1. 从 TOUCH_CST816S_REG_FINGER_NUM 开始读取 5 字节；
 *     2. 当 FingerNum > 0 时输出 pressed=true，并解析 x/y；
 *     3. 当 FingerNum == 0 时输出 pressed=false，x/y 清零。
 *
 * 参数：
 *     x：输出参数，保存解析后的 X 坐标；
 *     y：输出参数，保存解析后的 Y 坐标；
 *     pressed：输出参数，true 表示 FingerNum > 0。
 *
 * 调用方法：
 *     uint16_t x = 0;
 *     uint16_t y = 0;
 *     bool pressed = false;
 *
 *     if (touch_read(&x, &y, &pressed) == ESP_OK && pressed) {
 *         printf("x=%u, y=%u\n", x, y);
 *     }
 *
 * 返回：
 *     ESP_OK：读取成功；
 *     ESP_ERR_INVALID_ARG：输出参数为空；
 *     ESP_ERR_INVALID_STATE：尚未成功调用 touch_init()；
 *     其它值：I2C 读取失败。
 */
esp_err_t touch_read(uint16_t *x, uint16_t *y, bool *pressed);

/* touch_test_task：CST816S 触摸测试任务。
 *
 * 功能：
 *     1. 每 TOUCH_CST816S_TEST_PERIOD_MS 读取一次触摸状态；
 *     2. 仅当 FingerNum > 0，即 pressed=true 时打印：
 *            TOUCH:
 *            pressed=1
 *            x=%u
 *            y=%u
 *
 * 调用方法：
 *     xTaskCreate(touch_test_task,
 *                 TOUCH_CST816S_TEST_TASK_NAME,
 *                 TOUCH_CST816S_TEST_TASK_STACK_SIZE,
 *                 NULL,
 *                 TOUCH_CST816S_TEST_TASK_PRIORITY,
 *                 NULL);
 *
 * 注意：
 *     建议先调用 touch_init()，初始化成功后再创建本任务。
 *     如果外部直接创建本任务，任务内部也会尝试调用 touch_init()。
 */
void touch_test_task(void *arg);

#endif
