# 当前触摸芯片已确认是 CST816T，I2C 地址固定为 0x15。
# 只编译新的官方寄存器解析驱动，旧 CST816S 猜测解析文件保留但不进入固件。
idf_component_register(SRCS "touch_cst816t.c"
                    INCLUDE_DIRS "."
                    REQUIRES BSP)
