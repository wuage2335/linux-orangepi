# Orange Pi 5 Ultra 内核功能阅读指南

这份文档配合 [Orange Pi 5 Ultra 内核功能分类地图](./orangepi5-ultra-kernel-feature-map.md) 使用。功能地图回答“有什么、在哪里”，本文回答“这些功能做什么、从哪里开始看、沿什么链路看、会遇到什么数据结构”。

建议先记住一条主线：

```text
DTS 节点
  -> compatible
  -> of_device_id 匹配
  -> platform_device / 其他总线 device
  -> driver probe
  -> 子系统注册
  -> sysfs / devtmpfs / procfs / debugfs / 用户态接口
```

## 0. 通用阅读方法

- **先看板级 DTS**
  - 作用：确认 Orange Pi 5 Ultra 实际启用了哪些硬件。
  - 起点：[arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)
  - 链路：`节点名` -> `compatible` -> `rg compatible` -> 驱动 `of_match_table` -> `probe()`
  - 常见数据结构：`struct device_node`、`struct property`、`phandle`、`struct resource`

- **再看 defconfig**
  - 作用：确认某个功能是编进内核、编成模块，还是没有编译。
  - 起点：[arch/arm64/configs/rockchip_linux_defconfig](../arch/arm64/configs/rockchip_linux_defconfig)
  - 链路：`CONFIG_xxx` -> `Kconfig` -> `Makefile` -> 源文件
  - 常见数据结构：这里主要是配置符号，不是运行时数据结构。

- **最后看驱动 probe**
  - 作用：确认硬件如何被内核接管。
  - 起点：先找 `struct of_device_id`、`struct platform_driver`、`.probe`
  - 链路：`of_match_table` -> `probe()` -> `devm_*()` 资源申请 -> 子系统注册函数
  - 常见数据结构：`struct device`、`struct platform_device`、`struct platform_driver`、`struct of_device_id`

## 1. 板级与设备树

- **作用**
  - 描述 Orange Pi 5 Ultra 板子的硬件连接关系。
  - 决定哪些 SoC 节点启用、哪些外设挂在哪个 I2C/SPI/SDIO/PCIe/USB 总线上。
  - 提供 GPIO、IRQ、clock、regulator、pinctrl、PHY、memory region 等资源关系。

- **从哪里开始看**
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)
  - [arch/arm64/boot/dts/rockchip/rk3588.dtsi](../arch/arm64/boot/dts/rockchip/rk3588.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588-linux.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-linux.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588-rk806-single.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-rk806-single.dtsi)

- **查看链路**
  - `rk3588-orangepi-5-ultra.dts`
  - `#include "rk3588.dtsi"`
  - 找一个节点，例如 `gpio-leds`、`pwm-fan`、`&rknpu`、`&sdhci`
  - 看 `compatible`、`status`、`reg`、`interrupts`、`clocks`、`pinctrl-0`、`*-supply`
  - 用 `rg '"compatible字符串"' drivers Documentation` 找驱动和 binding

- **常见数据结构**
  - `struct device_node`：内核中的设备树节点。
  - `struct property`：设备树属性。
  - `phandle`：DTS 中 `&xxx` 引用的内部关系。
  - `struct resource`：由 `reg`、`interrupts` 等转换出的硬件资源。

## 2. 启动、设备模型与驱动匹配

- **作用**
  - 从 bootloader 进入 Linux 内核。
  - 解析 DTB。
  - 把设备树节点转换成 Linux driver model 里的 device。
  - 触发驱动匹配和 `probe()`。

- **从哪里开始看**
  - [arch/arm64/kernel/head.S](../arch/arm64/kernel/head.S)
  - [init/main.c](../init/main.c)
  - [arch/arm64/kernel/setup.c](../arch/arm64/kernel/setup.c)
  - [drivers/of/fdt.c](../drivers/of/fdt.c)
  - [drivers/of/platform.c](../drivers/of/platform.c)
  - [drivers/base/](../drivers/base/)

- **查看链路**
  - `head.S`
  - `start_kernel()`
  - `setup_arch()`
  - `unflatten_device_tree()`
  - `of_platform_default_populate()`
  - `platform_device` 创建
  - `platform_driver` 匹配
  - `probe()` 执行

- **常见数据结构**
  - `struct device`：所有设备的基础对象。
  - `struct device_driver`：所有驱动的基础对象。
  - `struct bus_type`：总线类型，例如 platform、PCI、USB、I2C、SPI。
  - `struct platform_device`：由设备树常见节点创建的平台设备。
  - `struct platform_driver`：平台驱动。
  - `struct of_device_id`：设备树 `compatible` 匹配表。

## 3. 配置与构建系统

- **作用**
  - 决定功能是否参与编译。
  - 决定驱动是内建 `=y`，还是模块 `=m`。
  - 决定最终生成 `Image`、`dtb`、`dtbo`、`.ko`。

- **从哪里开始看**
  - [Makefile](../Makefile)
  - [Kconfig](../Kconfig)
  - [arch/arm64/configs/rockchip_linux_defconfig](../arch/arm64/configs/rockchip_linux_defconfig)
  - [arch/arm64/configs/rk3588_linux.config](../arch/arm64/configs/rk3588_linux.config)
  - [arch/arm64/boot/dts/rockchip/Makefile](../arch/arm64/boot/dts/rockchip/Makefile)
  - [arch/arm64/boot/dts/rockchip/overlay/Makefile](../arch/arm64/boot/dts/rockchip/overlay/Makefile)

- **查看链路**
  - 在 defconfig 里找 `CONFIG_xxx`
  - 在 `Kconfig` 里找该配置项的依赖
  - 在 `Makefile` 里找 `obj-$(CONFIG_xxx)`
  - 打开对应 `.c` 文件

- **常见数据结构**
  - 构建阶段主要是配置和 Make 规则。
  - 运行时是否有数据结构，取决于被编进来的具体子系统。

## 4. Rockchip SoC 基础支撑

- **作用**
  - 让 Linux 能控制 RK3588 的基础硬件资源。
  - 包括 clock、reset、pinctrl、GPIO、power domain、GRF、IOMMU、PHY、OPP。
  - 这些是显示、USB、PCIe、NPU、视频、存储等高级功能的底座。

- **从哪里开始看**
  - [drivers/clk/rockchip/clk-rk3588.c](../drivers/clk/rockchip/clk-rk3588.c)
  - [drivers/pinctrl/pinctrl-rockchip.c](../drivers/pinctrl/pinctrl-rockchip.c)
  - [drivers/soc/rockchip/pm_domains.c](../drivers/soc/rockchip/pm_domains.c)
  - [drivers/soc/rockchip/grf.c](../drivers/soc/rockchip/grf.c)
  - [drivers/iommu/rockchip-iommu.c](../drivers/iommu/rockchip-iommu.c)
  - [drivers/phy/rockchip/](../drivers/phy/rockchip/)

- **查看链路**
  - 从 DTS 里的 `clocks`、`resets`、`power-domains`、`iommus`、`phys`、`pinctrl-0` 开始。
  - 找到对应 provider 节点，例如 `cru`、`pinctrl`、`power-controller`、`iommu`、`phy`。
  - 找 provider 的 `compatible`。
  - 进入 Rockchip 对应驱动。
  - 再回到 consumer 驱动，看它如何调用 `clk_get()`、`reset_control_get()`、`dev_pm_domain_attach()`、`iommu`、`phy_get()`。

- **常见数据结构**
  - `struct clk_hw`、`struct clk_ops`：clock 框架。
  - `struct reset_controller_dev`：reset 控制器。
  - `struct pinctrl_desc`、`struct pinctrl_dev`：pinctrl 框架。
  - `struct gpio_chip`：GPIO 控制器。
  - `struct generic_pm_domain`：power domain。
  - `struct iommu_ops`、`struct iommu_domain`：IOMMU。
  - `struct phy`、`struct phy_ops`：PHY 框架。

## 5. 电源、温控与频率管理

- **作用**
  - 控制电源轨、电压、CPU/GPU/NPU/DDR 频率、温度保护和挂起恢复。
  - 这部分直接影响稳定性、功耗和性能。

- **从哪里开始看**
  - [drivers/regulator/](../drivers/regulator/)
  - [drivers/cpufreq/rockchip-cpufreq.c](../drivers/cpufreq/rockchip-cpufreq.c)
  - [drivers/devfreq/rockchip_dmc.c](../drivers/devfreq/rockchip_dmc.c)
  - [drivers/devfreq/rockchip_bus.c](../drivers/devfreq/rockchip_bus.c)
  - [drivers/thermal/rockchip_thermal.c](../drivers/thermal/rockchip_thermal.c)
  - [drivers/soc/rockchip/rockchip_opp_select.c](../drivers/soc/rockchip/rockchip_opp_select.c)
  - [drivers/soc/rockchip/rockchip_pvtm.c](../drivers/soc/rockchip/rockchip_pvtm.c)

- **查看链路**
  - DTS 里找 `*-supply`、`operating-points-v2`、`thermal-zones`、`cooling-maps`。
  - regulator：`regulator-fixed` 或 PMIC 节点 -> regulator driver -> consumer `devm_regulator_get()`。
  - cpufreq：CPU OPP 表 -> cpufreq driver -> clock/regulator 调整。
  - thermal：温度传感器 -> thermal zone -> cooling device -> fan/cpufreq 降温。

- **常见数据结构**
  - `struct regulator_desc`、`struct regulator_dev`：regulator 框架。
  - `struct cpufreq_driver`、`struct cpufreq_policy`：CPU 频率。
  - `struct devfreq`、`struct devfreq_dev_profile`：设备频率。
  - `struct thermal_zone_device`：温区。
  - `struct thermal_cooling_device`：降温设备。
  - `struct dev_pm_opp`：OPP 电压频率点。

## 6. 存储、块设备与文件系统

- **作用**
  - 管理 SD、eMMC、SDIO、SPI flash、UFS、SATA/SSD、块设备和文件系统。
  - 决定系统能不能从存储介质启动、挂载 rootfs、读写数据。

- **从哪里开始看**
  - [drivers/mmc/host/dw_mmc-rockchip.c](../drivers/mmc/host/dw_mmc-rockchip.c)
  - [drivers/mmc/](../drivers/mmc/)
  - [drivers/mtd/](../drivers/mtd/)
  - [drivers/spi/spi-rockchip-sfc.c](../drivers/spi/spi-rockchip-sfc.c)
  - [drivers/ufs/host/ufs-rockchip.c](../drivers/ufs/host/ufs-rockchip.c)
  - [block/](../block/)
  - [fs/ext4/](../fs/ext4/)
  - [fs/f2fs/](../fs/f2fs/)
  - [fs/overlayfs/](../fs/overlayfs/)

- **查看链路**
  - DTS 里找 `&sdhci`、`&sdmmc`、`&sdio`、SPI flash、SATA overlay。
  - 控制器驱动 probe 后注册 host。
  - block layer 创建块设备。
  - VFS 选择文件系统。
  - `init/do_mounts.c` 挂载 rootfs。

- **常见数据结构**
  - `struct mmc_host`、`struct mmc_card`：MMC/SD/eMMC。
  - `struct request_queue`、`struct gendisk`、`struct bio`：块设备层。
  - `struct super_block`、`struct inode`、`struct dentry`、`struct file`：VFS。
  - `struct file_system_type`：文件系统注册。
  - `struct mtd_info`：MTD flash。

## 7. 网络、Wi-Fi、蓝牙与 CAN

- **作用**
  - 提供以太网、Wi-Fi、蓝牙、CAN、IPv4/IPv6、netfilter、bridge、VLAN 等能力。
  - Orange Pi 板子上，Wi-Fi/BT 通常还依赖外部 firmware。

- **从哪里开始看**
  - [net/](../net/)
  - [drivers/net/ethernet/stmicro/stmmac/](../drivers/net/ethernet/stmicro/stmmac/)
  - [drivers/net/phy/](../drivers/net/phy/)
  - [net/wireless/](../net/wireless/)
  - [net/mac80211/](../net/mac80211/)
  - [drivers/net/wireless/](../drivers/net/wireless/)
  - [net/bluetooth/](../net/bluetooth/)
  - [drivers/bluetooth/](../drivers/bluetooth/)
  - [drivers/net/can/rockchip/](../drivers/net/can/rockchip/)

- **查看链路**
  - Ethernet：DTS `gmac` 节点 -> STMMAC driver -> PHY driver -> `net_device` 注册。
  - Wi-Fi：DTS `wlan-platdata` / SDIO / PCIe -> Wi-Fi 芯片驱动 -> cfg80211/mac80211 -> `wlan0`。
  - Bluetooth：DTS 蓝牙节点 -> UART/USB/SDIO transport -> HCI driver -> Bluetooth core。
  - CAN：overlay 启用 CAN 节点 -> Rockchip CAN driver -> SocketCAN。

- **常见数据结构**
  - `struct net_device`：网络设备。
  - `struct sk_buff`：网络包。
  - `struct napi_struct`：NAPI 收包。
  - `struct phy_device`：以太网 PHY。
  - `struct wireless_dev`、`struct wiphy`：cfg80211。
  - `struct ieee80211_hw`：mac80211。
  - `struct hci_dev`：蓝牙 HCI。
  - `struct can_priv`：CAN 网络设备私有数据。

## 8. USB、Type-C 与 PCIe

- **作用**
  - 管理 USB host、USB gadget、Type-C 控制、USB PHY、PCIe host 和 PCIe 设备。
  - Wi-Fi、SSD、摄像头、外设扩展经常依赖这类高速接口。

- **从哪里开始看**
  - [drivers/usb/core/](../drivers/usb/core/)
  - [drivers/usb/dwc3/](../drivers/usb/dwc3/)
  - [drivers/usb/host/](../drivers/usb/host/)
  - [drivers/usb/gadget/](../drivers/usb/gadget/)
  - [drivers/usb/typec/](../drivers/usb/typec/)
  - [drivers/usb/typec/tcpm/fusb302.c](../drivers/usb/typec/tcpm/fusb302.c)
  - [drivers/pci/controller/pcie-rockchip-host.c](../drivers/pci/controller/pcie-rockchip-host.c)
  - [drivers/pci/controller/dwc/pcie-dw-rockchip.c](../drivers/pci/controller/dwc/pcie-dw-rockchip.c)
  - [drivers/phy/rockchip/](../drivers/phy/rockchip/)

- **查看链路**
  - USB：DTS `usbdrd` / `dwc3` / `u2phy` / `usbdp_phy` -> PHY 初始化 -> DWC3 -> xHCI 或 gadget。
  - Type-C：I2C 上的 `fusb302` -> TCPM -> role/power negotiation -> USB role。
  - PCIe：DTS `pcie` 节点 -> PCIe host driver -> PHY/clock/reset -> 枚举 PCIe 设备。

- **常见数据结构**
  - `struct usb_device`、`struct usb_interface`、`struct usb_driver`：USB 设备模型。
  - `struct usb_hcd`：USB host controller。
  - `struct usb_gadget`、`struct usb_ep`：USB gadget。
  - `struct typec_port`、`struct tcpm_port`：Type-C/TCPM。
  - `struct pci_dev`、`struct pci_driver`、`struct pci_host_bridge`：PCIe。
  - `struct phy`：USB/PCIe PHY。

## 9. 低速外设、输入、LED 与风扇

- **作用**
  - 提供 I2C、SPI、UART、PWM、ADC、LED、按键、触摸、风扇等板级常见功能。
  - 这些模块最适合新手练习 `DTS -> driver -> probe`。

- **从哪里开始看**
  - [drivers/i2c/](../drivers/i2c/)
  - [drivers/spi/spi-rockchip.c](../drivers/spi/spi-rockchip.c)
  - [drivers/tty/serial/](../drivers/tty/serial/)
  - [drivers/pwm/pwm-rockchip.c](../drivers/pwm/pwm-rockchip.c)
  - [drivers/iio/adc/rockchip_saradc.c](../drivers/iio/adc/rockchip_saradc.c)
  - [drivers/input/keyboard/adc-keys.c](../drivers/input/keyboard/adc-keys.c)
  - [drivers/leds/leds-gpio.c](../drivers/leds/leds-gpio.c)
  - [drivers/hwmon/pwm-fan.c](../drivers/hwmon/pwm-fan.c)

- **查看链路**
  - LED：DTS `gpio-leds` -> `leds-gpio.c` -> `gpio_led_probe()` -> LED class。
  - 风扇：DTS `pwm-fan` -> `pwm-fan.c` -> `pwm_fan_probe()` -> hwmon / cooling device。
  - ADC keys：DTS `adc-keys` -> `adc-keys.c` -> input device。
  - SPI/I2C/UART/PWM：DTS 控制器节点 -> Rockchip controller driver -> 子设备或用户态接口。

- **常见数据结构**
  - `struct i2c_adapter`、`struct i2c_client`、`struct i2c_driver`：I2C。
  - `struct spi_controller`、`struct spi_device`、`struct spi_driver`：SPI。
  - `struct uart_port`、`struct uart_driver`：UART。
  - `struct pwm_chip`、`struct pwm_device`：PWM。
  - `struct iio_dev`：IIO/ADC。
  - `struct input_dev`：输入设备。
  - `struct led_classdev`：LED。
  - `struct hwmon_chip_info`、`struct thermal_cooling_device`：hwmon/风扇。

## 10. 显示、DRM、HDMI、MIPI 与 GPU

- **作用**
  - 控制显示输出、显示 pipeline、HDMI/eDP/DP/MIPI DSI、LCD backlight、GPU。
  - 这部分层级深，新手不建议第一个看。

- **从哪里开始看**
  - [drivers/gpu/drm/](../drivers/gpu/drm/)
  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - [drivers/gpu/drm/panel/](../drivers/gpu/drm/panel/)
  - [drivers/video/backlight/](../drivers/video/backlight/)
  - [drivers/phy/rockchip/phy-rockchip-samsung-hdptx-hdmi.c](../drivers/phy/rockchip/phy-rockchip-samsung-hdptx-hdmi.c)
  - [drivers/gpu/arm/bifrost/](../drivers/gpu/arm/bifrost/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-lcd.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-lcd.dts)

- **查看链路**
  - DTS `vop` / `display_subsystem` / `hdmi` / `dsi` / `panel` / `backlight`。
  - Rockchip DRM platform driver 初始化。
  - component framework 绑定 VOP、encoder、connector、panel。
  - DRM core 创建 `/dev/dri/card*`。
  - GPU 走 Mali 驱动，用户态还需要对应 Mali 库。

- **常见数据结构**
  - `struct drm_device`、`struct drm_driver`：DRM 设备和驱动。
  - `struct drm_crtc`、`struct drm_plane`、`struct drm_encoder`、`struct drm_connector`：显示 pipeline。
  - `struct drm_panel`：屏幕面板。
  - `struct backlight_device`：背光。
  - `struct component_ops`：多驱动组件绑定。
  - Mali 驱动中常见 `kbase_device`、`kbase_context` 等私有结构。

## 11. 摄像头、ISP、HDMI-IN 与 V4L2

- **作用**
  - 处理摄像头输入、MIPI CSI、CIF、ISP、ISPP、HDMI-IN 和 V4L2 用户态接口。
  - 这部分同样比较复杂，通常要结合 sensor、DTS、media graph 一起看。

- **从哪里开始看**
  - [drivers/media/](../drivers/media/)
  - [drivers/media/platform/rockchip/cif/](../drivers/media/platform/rockchip/cif/)
  - [drivers/media/platform/rockchip/isp/](../drivers/media/platform/rockchip/isp/)
  - [drivers/media/platform/rockchip/isp1/](../drivers/media/platform/rockchip/isp1/)
  - [drivers/media/platform/rockchip/rkisp1/](../drivers/media/platform/rockchip/rkisp1/)
  - [drivers/media/platform/rockchip/ispp/](../drivers/media/platform/rockchip/ispp/)
  - [drivers/media/platform/rockchip/hdmirx/](../drivers/media/platform/rockchip/hdmirx/)
  - [drivers/media/i2c/](../drivers/media/i2c/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam0.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam0.dts)

- **查看链路**
  - DTS camera overlay。
  - sensor 节点。
  - MIPI CSI / DPHY。
  - CIF 或 ISP。
  - media controller graph。
  - V4L2 video node。
  - 用户态通过 `/dev/video*` 使用。

- **常见数据结构**
  - `struct v4l2_device`：V4L2 设备。
  - `struct video_device`：`/dev/video*` 节点。
  - `struct v4l2_subdev`：sensor、CSI、ISP 等子设备。
  - `struct media_device`、`struct media_entity`、`struct media_pad`：media graph。
  - `struct vb2_queue`：videobuf2 缓冲队列。

## 12. 视频编解码、RGA、MPP 与 NPU

- **作用**
  - 提供硬件视频编解码、图像处理、RGA 加速、MPP 服务和 NPU 推理接口。
  - 注意：内核只提供驱动接口，真正使用通常依赖 Rockchip 用户态库。

- **从哪里开始看**
  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)
  - [drivers/video/rockchip/mpp_osal/](../drivers/video/rockchip/mpp_osal/)
  - [drivers/video/rockchip/rga/](../drivers/video/rockchip/rga/)
  - [drivers/video/rockchip/rga2/](../drivers/video/rockchip/rga2/)
  - [drivers/video/rockchip/rga3/](../drivers/video/rockchip/rga3/)
  - [drivers/staging/media/rkvdec/](../drivers/staging/media/rkvdec/)
  - [drivers/rknpu/](../drivers/rknpu/)
  - [drivers/rknpu/include/](../drivers/rknpu/include/)

- **查看链路**
  - DTS 启用 `mpp_srv`、`rkvdec`、`rkvenc`、`jpege`、`jpegd`、`rga`、`rknpu`。
  - 驱动 probe 后注册 misc/char/V4L2/专用接口。
  - 用户态库打开设备节点。
  - 用户态提交任务和 buffer。
  - IOMMU/DMA 负责地址映射。
  - 硬件完成后中断返回结果。

- **常见数据结构**
  - `struct platform_device`、`struct miscdevice`、`struct file_operations`：常见设备接口。
  - `struct dma_buf`、`struct sg_table`：跨设备共享 buffer。
  - `struct iommu_domain`：IOMMU 地址空间。
  - RGA/MPP/RKNPU 各自有大量私有 task/session/context 结构，阅读时先找 `ctx`、`session`、`task`、`job` 命名。

## 13. 音频

- **作用**
  - 管理 I2S/TDM、PDM、SPDIF、HDMI audio、ES8388 codec、声卡注册和耳机检测。
  - ASoC 把 CPU DAI、codec DAI、machine driver 组合成声卡。

- **从哪里开始看**
  - [sound/soc/](../sound/soc/)
  - [sound/soc/rockchip/](../sound/soc/rockchip/)
  - [sound/soc/codecs/](../sound/soc/codecs/)
  - [drivers/headset_observe/](../drivers/headset_observe/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **查看链路**
  - DTS `es8388-sound`、`hdmi1-sound`、`i2s`、`spdif`。
  - codec driver 注册 codec component。
  - Rockchip I2S/SPDIF driver 注册 CPU DAI。
  - machine driver 绑定 CPU DAI 和 codec DAI。
  - ALSA 创建声卡设备。

- **常见数据结构**
  - `struct snd_soc_card`：声卡。
  - `struct snd_soc_dai_link`：CPU DAI 和 codec DAI 的连接。
  - `struct snd_soc_component_driver`：codec 或平台组件。
  - `struct snd_soc_dai_driver`：DAI 驱动。
  - `struct snd_pcm_substream`：PCM 播放/录音流。

## 14. 安全、容器与虚拟化

- **作用**
  - 提供 LSM、安全策略、keyring、crypto、dm-crypt、cgroup、namespace、KVM 等基础能力。
  - 这类功能通常不绑定 Orange Pi 5 Ultra 板级 DTS，而是由内核配置决定。

- **从哪里开始看**
  - [security/](../security/)
  - [security/keys/](../security/keys/)
  - [crypto/](../crypto/)
  - [drivers/crypto/rockchip/](../drivers/crypto/rockchip/)
  - [kernel/cgroup/](../kernel/cgroup/)
  - [net/netfilter/](../net/netfilter/)
  - [fs/overlayfs/](../fs/overlayfs/)
  - [arch/arm64/kvm/](../arch/arm64/kvm/)
  - [virt/kvm/](../virt/kvm/)

- **查看链路**
  - 安全：系统调用或文件操作 -> LSM hook -> security module。
  - 容器：namespace 隔离 -> cgroup 资源限制 -> overlayfs 文件层 -> netfilter 网络规则。
  - KVM：用户态 VMM -> `/dev/kvm` -> KVM ioctl -> ARM64 virtualization。

- **常见数据结构**
  - `struct cred`、`struct user_namespace`：权限和用户命名空间。
  - `struct cgroup`、`struct cgroup_subsys_state`：cgroup。
  - `struct nsproxy`：进程命名空间集合。
  - `struct crypto_alg`：加密算法。
  - `struct kvm`、`struct kvm_vcpu`：KVM 虚拟机和 vCPU。

## 15. 调试、追踪与诊断

- **作用**
  - 用于定位启动失败、驱动 probe 失败、硬件异常、性能问题和内核崩溃。
  - 对读内核也很重要，因为很多行为只能靠日志和 trace 确认。

- **从哪里开始看**
  - [kernel/printk/](../kernel/printk/)
  - [kernel/trace/](../kernel/trace/)
  - [kernel/events/](../kernel/events/)
  - [kernel/kprobes.c](../kernel/kprobes.c)
  - [fs/debugfs/](../fs/debugfs/)
  - [drivers/soc/rockchip/rockchip_debug.c](../drivers/soc/rockchip/rockchip_debug.c)
  - [drivers/soc/rockchip/minidump/](../drivers/soc/rockchip/minidump/)
  - [drivers/soc/rockchip/fiq_debugger/](../drivers/soc/rockchip/fiq_debugger/)

- **查看链路**
  - 启动日志：`printk()` -> console -> `dmesg`。
  - 动态调试：driver `dev_dbg()` -> dynamic debug 开关。
  - trace：tracepoint/ftrace/perf -> trace buffer -> 用户态读取。
  - 崩溃：panic/oops -> stack trace -> minidump 或 crash dump。

- **常见数据结构**
  - `struct printk_info`：printk 日志元数据。
  - `struct trace_event_call`：tracepoint 事件。
  - `struct perf_event`：perf 事件。
  - `struct kprobe`：kprobe。
  - `struct dentry`：debugfs/procfs/sysfs 中也经常出现。

## 16. 最适合新手的阅读顺序

- **第一条链路：LED**
  - 起点：[arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)
  - 驱动：[drivers/leds/leds-gpio.c](../drivers/leds/leds-gpio.c)
  - 链路：`gpio-leds` -> `of_gpio_leds_match` -> `gpio_led_probe()` -> `led_classdev`

- **第二条链路：PWM fan**
  - 起点：[arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)
  - 驱动：[drivers/hwmon/pwm-fan.c](../drivers/hwmon/pwm-fan.c)
  - 链路：`pwm-fan` -> `of_pwm_fan_match` -> `pwm_fan_probe()` -> `hwmon` / `thermal_cooling_device`

- **第三条链路：ADC keys**
  - 起点：[arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)
  - 驱动：[drivers/input/keyboard/adc-keys.c](../drivers/input/keyboard/adc-keys.c)
  - 链路：`adc-keys` -> SARADC channel -> input device -> key event

- **第四条链路：SD/eMMC**
  - 起点：[arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)
  - 驱动：[drivers/mmc/host/dw_mmc-rockchip.c](../drivers/mmc/host/dw_mmc-rockchip.c)
  - 链路：`sdhci/sdmmc/sdio` -> MMC host -> block device -> filesystem

- **第五条链路：显示或摄像头**
  - 显示起点：[drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - 摄像头起点：[drivers/media/platform/rockchip/](../drivers/media/platform/rockchip/)
  - 建议等前四条链路读懂后再看。

## 17. 常用搜索命令

```bash
# 找 Orange Pi 5 Ultra 板级节点
rg -n "orangepi-5-ultra|gpio-leds|pwm-fan|adc-keys|rknpu|mpp_srv|hdmi|sdhci|sdmmc|sdio" arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts

# 用 compatible 找驱动
rg -n '"gpio-leds"|of_match_table|platform_driver|probe' drivers Documentation

# 从配置找源码
rg -n "CONFIG_DRM_ROCKCHIP|CONFIG_ROCKCHIP_RKNPU|CONFIG_USB_DWC3|CONFIG_LEDS_GPIO" arch/arm64/configs drivers

# 找某个 probe
rg -n "gpio_led_probe|pwm_fan_probe|rockchip_saradc_probe|dw_mci_rockchip_probe|dwc3_probe" drivers

# 找核心数据结构定义
rg -n "struct platform_driver|struct net_device|struct drm_device|struct v4l2_subdev|struct snd_soc_card" include drivers sound
```

