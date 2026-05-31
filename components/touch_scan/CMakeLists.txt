# touch_scan 是早期用于确认触摸芯片 I2C 地址的临时扫描组件。
# 现在已确认触摸芯片为 CST816S，I2C 地址固定为 0x15，因此停止编译扫描源码，
# 也不再向 main 注入 touch_scan.h，避免地址扫描逻辑进入固件执行/链接路径。
idf_component_register(INCLUDE_DIRS ".")
