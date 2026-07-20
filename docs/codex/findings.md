# linux-orangepi 阅读发现

## 项目定位

- 目标项目名：`linux-orangepi`。
- Linux 内部路径：`/home/wuage2335/linux-orangepi`。
- Windows/Explorer 访问路径应为：`\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi`。
- 用户确认 WSL 可通过 `\\wsl.localhost\Ubuntu-22.04\` 访问，也可在 PowerShell 中使用 `wsl -d Ubuntu-22.04` 打开终端；执行单条命令可使用 `wsl -d Ubuntu-22.04 -- sh -lc '...'`。后续操作应优先使用这两种入口。
- 此前 Codex PowerShell 进程直接访问上述 UNC 路径时返回 `Access is denied`，直接调用 `wsl.exe` 也报告无可用发行版；这应理解为当时 Codex 沙盒/进程权限限制，不代表路径错误。
- Ubuntu 22.04 WSL 的底层数据文件位于 `C:\Users\Administrator\AppData\Local\Packages\CanonicalGroupLimited.Ubuntu22.04LTS_79rhkp1fndgsc\LocalState\ext4.vhdx`；我上一轮通过只读解析 VHDX/ext4 确认了 Linux 内部路径，但这是受限环境下的 fallback，不是首选访问方式。
- Windows 侧还存在两个同名副本：`H:\Embedded\ov13850_driver\.external\linux-orangepi` 和 `H:\Embedded\ov13850_driver\reports\tmp_stage2_contract\cache\linux-orangepi`，但它们只包含 `.git` 与 `drivers`，不是完整内核树。

## 文档线索

待补充。

## 构建与运行线索

- 顶层 `Makefile` 显示内核版本为 `6.1.99`，`NAME = Curry Ramen`。
- `README` 是标准 Linux kernel README，建议从 `Documentation/admin-guide/README.rst` 与 `Documentation/process/changes.rst` 开始。
- 主构建系统是标准 Kbuild：`make ARCH=arm64 ...`、`O=`/`KBUILD_OUTPUT`、`CROSS_COMPILE` 等机制。
- OrangePi 5 Ultra 主 DTB 入口在 `arch/arm64/boot/dts/rockchip/Makefile`：`rk3588-orangepi-5-ultra.dtb`。
- 可选 DTBO overlay 在 `arch/arm64/boot/dts/rockchip/overlay/Makefile`，包含 I2C/SPI/UART/PWM/CAN/LCD/CAM/HDMI/SSD/Wi-Fi/disable-led 等。
- `boot.its` 描述 Rockchip FIT 镜像，包含 `fdt`、`kernel`、`resource` 三个 image，并配置 sha256/rsa2048 签名。

## 目录与模块

- WSL 项目是完整 Linux 内核源码树，顶层包含 `Documentation`、`arch`、`block`、`drivers`、`fs`、`include`、`kernel`、`lib`、`mm`、`net`、`scripts`、`tools` 等标准目录。
- 顶层有 OrangePi 相关文件 `boot.its`，另有项目自带 `docs` 目录。
- `docs/orangepi5-ultra-kernel-reading-guide.md` 给出的主线是：DTS 节点 -> compatible -> of_device_id 匹配 -> device/driver -> probe -> 子系统注册 -> sysfs/devtmpfs/procfs/debugfs/用户接口。
- `docs/orangepi5-ultra-kernel-feature-map.md` 将项目分成板级 DTS、启动、Kbuild/Kconfig、Rockchip SoC 基础支撑、电源热管理、存储、网络、USB/PCIe、低速外设、显示、摄像头/ISP/V4L2、MPP/RGA/NPU、音频、安全/容器/虚拟化、调试诊断等。
- 主设备树 `rk3588-orangepi-5-ultra.dts` 启用/配置了 ADC keys、PWM backlight、HDMI1、ES8388 音频、PCIe、Wi-Fi/BT、GPIO LED、PWM fan、HDMI-IN、GPU、RKNPU、MPP/RGA、SD/eMMC/SDIO、USB、VOP、camera/ISP、SFC、watchdog 等。
- `arch/arm64/configs/rockchip_linux_defconfig` 包含 `CONFIG_ARCH_ROCKCHIP=y`、`CONFIG_DRM_ROCKCHIP=y`、`CONFIG_VIDEO_ROCKCHIP_*`、`CONFIG_ROCKCHIP_MPP_*`、`CONFIG_ROCKCHIP_RKNPU=y`、`CONFIG_USB_DWC3=y`、`CONFIG_MMC_DW_ROCKCHIP=y`、`CONFIG_LEDS_GPIO=y` 等关键配置。
- `drivers/rknpu` 由 `rknpu_drv/reset/job/debugger/iommu/devfreq/fence/gem/mem` 等文件组成，Kconfig 支持 DRM GEM 或 Rockchip DMA heap 作为内存管理路径。
- OV13850 sensor 驱动位于 `drivers/media/i2c/ov13850.c`，是 V4L2 subdev/I2C driver，支持 2112x1568@30fps 和 4224x3136@7fps，probe 中获取 module 信息、clock/GPIO/regulator/pinctrl，注册 async sensor subdev。
- WSL `main` 还包含 `drivers/media/i2c/ov13850_i2c_min.c`，Kconfig 名为 `VIDEO_OV13850_I2C_MIN`，compatible 为 `learning,ov13850-i2c`；它是一个最小学习/bring-up 驱动，提供 sysfs 寄存器读写、chip id/revision 查询、mode/full_init 触发，并注册 V4L2 sensor subdev。
- `rockchip_linux_defconfig` 默认启用 `CONFIG_VIDEO_OV13850=y` 和 `CONFIG_VIDEO_OV13855=y`，但没有启用 `VIDEO_OV13850_I2C_MIN`；若要使用最小学习驱动，需要手动打开配置并在 DTS/overlay 中使用其独立 compatible。
- Camera dtsi `rk3588-orangepi-5-max-camera0.dtsi` 中 OV13850 默认 compatible 是 `ovti,ov13850`、I2C 地址 `0x10`、2-lane MIPI，默认 `status = "disabled"`；`rk3588-opi5ultra-cam*.dts` overlay 负责打开 DPHY/CSI/CIF/ISP/i2c/sensor 节点。
- Windows `H:\Embedded\ov13850_driver\.external\linux-orangepi` 是 `stage2-fetch` 分支的 partial media 副本，包含 `ov13850_new.c`、`ov13850_regs.h`、`ov13850_modes.h`，Kconfig 名为 `VIDEO_OV13850_NEW`，与 WSL `main` 的 `ov13850_i2c_min.c` 不是同一实验形态。

## 待确认问题

- 当前 Codex 环境为什么无法直接访问 `\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi`，以及为什么 `wsl.exe --list` 仍报告无可用发行版。
- 后续若需要直接修改 WSL 项目，优先在权限允许时使用 UNC 路径；若当前 Codex 仍被拒绝，则需要用户确认是否切换到可访问 WSL 的终端/会话。
- 离线 VHDX/ext4 解析脚本读取部分文件时返回全零，需要后续若要深读这些文件，最好在可运行 WSL 内直接使用 `rg/cat/git`。

## 摄像头链路实施状态

- 用户确认“基线验证”和“传感器与 DTS”两个阶段已经完成，后续不再重复安排。
- 当前实施入口调整为 V4L2 sensor subdev 驱动完善，之后依次进入 ISP/RGA、MPP、低延迟推流和性能稳定性。
- 阶段计划统一使用阶段编号，不再附带周或天等时间估算。

## `ov13850_i2c_min.c` 阶段 2 审查

- 该学习驱动已具备：I2C 多字节读/单字节写、寄存器表写入、R2A revision 选择、global/mode 初始化、上电与下电、芯片 ID 检查、sysfs 寄存器调试、单 pad V4L2 Subdev、固定 RAW10 格式和异步 sensor 注册。
- 当前至少存在两个源码级编译阻塞：`struct ov13850_mode` 缺少 `max_fps` 成员但初始化和帧间隔查询都在使用它；video ops 引用了未定义的 `ov13850_min_g_mbus_config`，实际已定义函数名是 `ov13850_min_get_mbus_config`。此外，正式驱动只在 pad ops 中挂接 `get_mbus_config`。
- 存在高优先级 clientdata 生命周期问题：probe 前段把 `i2c_set_clientdata(client, cam)` 设为私有结构体，但后续 `v4l2_i2c_subdev_init()` 会按 V4L2 I2C Subdev 约定将 clientdata 设为 `struct v4l2_subdev *`。当前 sysfs 回调和 `remove()` 仍把 `i2c_get_clientdata()` 直接当成 `struct ov13850_min *`，会得到错误对象地址。应统一采用“取出 subdev，再通过 `to_ov13850_min(sd)` 回到私有结构体”的正式驱动模式，并在 V4L2 初始化完成后再暴露依赖它的 sysfs 接口。
- 当前没有 `.s_stream`，而 `full_init` 和 mode 表会强制保持 `0x0100 = 0x00`；因此媒体管线无法通过 V4L2 正式启动 Sensor 输出，这是编译修通后的第一功能缺口。
- 当前没有 `v4l2_ctrl_handler`，缺少 link frequency、pixel rate、HBLANK、VBLANK、曝光、模拟增益和测试图等 Controls，也没有在 stream on 前统一应用 Controls。
- 当前没有 runtime PM；probe 成功后 Sensor 长期保持上电。后续加入 PM 时，sysfs 寄存器访问也必须持有 PM 引用，不能在掉电状态直接 I2C 访问。
- 当前 `set_fmt/get_fmt` 没有区分 TRY 与 ACTIVE，只有单一固定模式；还缺少 `enum_frame_interval`、模式数组/最佳匹配以及切换模式时的控制范围更新。
- 其他后续完善项包括 pinctrl default/sleep、时钟实际频率检查、power_on 失败路径回滚、streaming 状态、Rockchip module info，以及收敛大量 `dev_info` 和原始 sysfs 写寄存器接口。
- 模式的 2112x1568、HTS/VTS、30 fps 与 120 MHz pixel rate 常量均直接继承自同仓库正式驱动；不能仅凭通用 `pixel_rate/(HTS*VTS)` 公式断定帧率错误，需结合 OV13850 的时序单位和实测验证。
