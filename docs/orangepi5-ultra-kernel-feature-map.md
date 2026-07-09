# Orange Pi 5 Ultra 内核功能分类地图

本文按功能分类整理当前 Linux/Rockchip 内核源码中与 Orange Pi 5 Ultra 制作相关的主要内容。每个功能项下方列出相对路径，便于在仓库中直接跳转。

说明：

- 板级启用状态主要由 `arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts` 决定。
- 是否编进内核或编成模块主要由 `arch/arm64/configs/rockchip_linux_defconfig`、`arch/arm64/configs/rk3588_linux.config`、`arch/arm64/configs/defconfig` 决定。
- 完整可烧录系统不只需要内核，还需要 bootloader、rootfs、firmware、用户态库和镜像打包脚本。

## 1. 板级与设备树

- **Orange Pi 5 Ultra 主设备树**

  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **RK3588 SoC 公共设备树**

  - [arch/arm64/boot/dts/rockchip/rk3588.dtsi](../arch/arm64/boot/dts/rockchip/rk3588.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588s.dtsi](../arch/arm64/boot/dts/rockchip/rk3588s.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588-linux.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-linux.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588-rk806-single.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-rk806-single.dtsi)

- **Orange Pi 5 Ultra DTB 构建入口**

  - [arch/arm64/boot/dts/rockchip/Makefile](../arch/arm64/boot/dts/rockchip/Makefile)

- **Orange Pi 5 Ultra 相关 DTBO overlay**

  - [arch/arm64/boot/dts/rockchip/overlay/Makefile](../arch/arm64/boot/dts/rockchip/overlay/Makefile)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-lcd.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-lcd.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam0.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam0.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam1.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam1.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam2.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam2.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-disable-leds.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-disable-leds.dts)

- **设备树 binding 文档**

  - [Documentation/devicetree/bindings/](../Documentation/devicetree/bindings/)
  - [include/dt-bindings/](../include/dt-bindings/)

## 2. 架构启动与内核入口

- **ARM64 早期启动**

  - [arch/arm64/kernel/head.S](../arch/arm64/kernel/head.S)
  - [arch/arm64/kernel/setup.c](../arch/arm64/kernel/setup.c)
  - [arch/arm64/mm/](../arch/arm64/mm/)
  - [arch/arm64/include/](../arch/arm64/include/)

- **Linux 内核主入口**

  - [init/main.c](../init/main.c)
  - [init/](../init/)

- **设备树解析与 platform device 创建**

  - [drivers/of/fdt.c](../drivers/of/fdt.c)
  - [drivers/of/platform.c](../drivers/of/platform.c)
  - [drivers/of/](../drivers/of/)

- **ARM64 异常、中断、系统调用相关**

  - [arch/arm64/kernel/](../arch/arm64/kernel/)
  - [arch/arm64/include/asm/](../arch/arm64/include/asm/)

## 3. 内核核心能力

- **进程、线程、调度**

  - [kernel/sched/](../kernel/sched/)
  - [kernel/fork.c](../kernel/fork.c)
  - [kernel/exit.c](../kernel/exit.c)
  - [kernel/signal.c](../kernel/signal.c)

- **内存管理**

  - [mm/](../mm/)
  - [arch/arm64/mm/](../arch/arm64/mm/)
  - [include/linux/mm.h](../include/linux/mm.h)

- **中断、定时器、时间管理**

  - [kernel/irq/](../kernel/irq/)
  - [kernel/time/](../kernel/time/)
  - [drivers/clocksource/](../drivers/clocksource/)
  - [drivers/clocksource/timer-rockchip.c](../drivers/clocksource/timer-rockchip.c)

- **RCU、workqueue、kthread**

  - [kernel/rcu/](../kernel/rcu/)
  - [kernel/workqueue.c](../kernel/workqueue.c)
  - [kernel/kthread.c](../kernel/kthread.c)

- **内核模块机制**

  - [kernel/module/](../kernel/module/)
  - [include/linux/module.h](../include/linux/module.h)

- **initramfs 和根文件系统挂载**

  - [init/initramfs.c](../init/initramfs.c)
  - [init/do_mounts.c](../init/do_mounts.c)
  - [init/do_mounts_initrd.c](../init/do_mounts_initrd.c)

## 4. 配置与构建系统

- **Kbuild / Makefile**

  - [Makefile](../Makefile)
  - [Kbuild](../Kbuild)
  - [scripts/](../scripts/)

- **Kconfig 配置系统**

  - [Kconfig](../Kconfig)
  - [arch/arm64/Kconfig](../arch/arm64/Kconfig)
  - [drivers/*/Kconfig](../drivers/)

- **Rockchip / RK3588 配置**

  - [arch/arm64/configs/rockchip_linux_defconfig](../arch/arm64/configs/rockchip_linux_defconfig)
  - [arch/arm64/configs/rk3588_linux.config](../arch/arm64/configs/rk3588_linux.config)
  - [arch/arm64/configs/defconfig](../arch/arm64/configs/defconfig)

- **FIT 镜像描述**

  - [boot.its](../boot.its)

## 5. Rockchip RK3588 SoC 基础支撑

- **clock / reset**

  - [drivers/clk/rockchip/](../drivers/clk/rockchip/)
  - [drivers/clk/rockchip/clk-rk3588.c](../drivers/clk/rockchip/clk-rk3588.c)
  - [include/dt-bindings/clock/rk3588-cru.h](../include/dt-bindings/clock/rk3588-cru.h)

- **pinctrl / iomux / GPIO 复用**

  - [drivers/pinctrl/pinctrl-rockchip.c](../drivers/pinctrl/pinctrl-rockchip.c)
  - [drivers/pinctrl/](../drivers/pinctrl/)
  - [drivers/soc/rockchip/iomux.c](../drivers/soc/rockchip/iomux.c)
  - [include/dt-bindings/pinctrl/rockchip.h](../include/dt-bindings/pinctrl/rockchip.h)

- **GPIO**

  - [drivers/gpio/](../drivers/gpio/)
  - [drivers/pinctrl/pinctrl-rockchip.c](../drivers/pinctrl/pinctrl-rockchip.c)

- **power domain**

  - [drivers/soc/rockchip/pm_domains.c](../drivers/soc/rockchip/pm_domains.c)
  - [drivers/soc/rockchip/](../drivers/soc/rockchip/)

- **GRF / PMU / system register**

  - [drivers/soc/rockchip/grf.c](../drivers/soc/rockchip/grf.c)
  - [drivers/soc/rockchip/](../drivers/soc/rockchip/)

- **IOMMU**

  - [drivers/iommu/](../drivers/iommu/)
  - [drivers/iommu/rockchip-iommu.c](../drivers/iommu/rockchip-iommu.c)

- **OPP / PVTM / 性能调节**

  - [drivers/opp/](../drivers/opp/)
  - [drivers/soc/rockchip/rockchip_opp_select.c](../drivers/soc/rockchip/rockchip_opp_select.c)
  - [drivers/soc/rockchip/rockchip_pvtm.c](../drivers/soc/rockchip/rockchip_pvtm.c)
  - [drivers/soc/rockchip/rockchip_performance.c](../drivers/soc/rockchip/rockchip_performance.c)

- **CPU 信息与 SoC 信息**

  - [drivers/soc/rockchip/rockchip-cpuinfo.c](../drivers/soc/rockchip/rockchip-cpuinfo.c)
  - [drivers/soc/rockchip/](../drivers/soc/rockchip/)

## 6. 电源、温控与频率管理

- **regulator 电源**

  - [drivers/regulator/](../drivers/regulator/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **cpufreq**

  - [drivers/cpufreq/](../drivers/cpufreq/)
  - [drivers/cpufreq/rockchip-cpufreq.c](../drivers/cpufreq/rockchip-cpufreq.c)

- **devfreq / DMC / DDR 频率**

  - [drivers/devfreq/](../drivers/devfreq/)
  - [drivers/devfreq/rockchip_dmc.c](../drivers/devfreq/rockchip_dmc.c)
  - [drivers/devfreq/rockchip_bus.c](../drivers/devfreq/rockchip_bus.c)
  - [drivers/devfreq/event/rockchip-dfi.c](../drivers/devfreq/event/rockchip-dfi.c)

- **thermal / 温度传感器**

  - [drivers/thermal/](../drivers/thermal/)
  - [drivers/thermal/rockchip_thermal.c](../drivers/thermal/rockchip_thermal.c)

- **suspend / resume**

  - [drivers/soc/rockchip/rockchip_pm_config.c](../drivers/soc/rockchip/rockchip_pm_config.c)
  - [drivers/soc/rockchip/pm_domains.c](../drivers/soc/rockchip/pm_domains.c)
  - [arch/arm64/kernel/suspend.c](../arch/arm64/kernel/suspend.c)
  - [arch/arm64/kernel/sleep.S](../arch/arm64/kernel/sleep.S)

- **watchdog**

  - [drivers/watchdog/](../drivers/watchdog/)

## 7. 存储与块设备

- **SD / eMMC / SDIO**

  - [drivers/mmc/](../drivers/mmc/)
  - [drivers/mmc/host/dw_mmc-rockchip.c](../drivers/mmc/host/dw_mmc-rockchip.c)
  - [drivers/phy/rockchip/phy-rockchip-emmc.c](../drivers/phy/rockchip/phy-rockchip-emmc.c)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **SPI NOR / SPI NAND / SFC**

  - [drivers/spi/](../drivers/spi/)
  - [drivers/spi/spi-rockchip.c](../drivers/spi/spi-rockchip.c)
  - [drivers/spi/spi-rockchip-sfc.c](../drivers/spi/spi-rockchip-sfc.c)
  - [drivers/mtd/](../drivers/mtd/)

- **UFS**

  - [drivers/ufs/](../drivers/ufs/)
  - [drivers/ufs/host/ufs-rockchip.c](../drivers/ufs/host/ufs-rockchip.c)

- **SATA / SSD overlay**

  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-ssd-sata0.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-ssd-sata0.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-ssd-sata2.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-ssd-sata2.dts)

- **block layer**

  - [block/](../block/)
  - [drivers/block/](../drivers/block/)

- **vendor storage**

  - [drivers/soc/rockchip/rk_vendor_storage.c](../drivers/soc/rockchip/rk_vendor_storage.c)
  - [drivers/soc/rockchip/flash_vendor_storage.c](../drivers/soc/rockchip/flash_vendor_storage.c)
  - [drivers/soc/rockchip/sdmmc_vendor_storage.c](../drivers/soc/rockchip/sdmmc_vendor_storage.c)
  - [drivers/soc/rockchip/mtd_vendor_storage.c](../drivers/soc/rockchip/mtd_vendor_storage.c)
  - [drivers/soc/rockchip/ram_vendor_storage.c](../drivers/soc/rockchip/ram_vendor_storage.c)

## 8. 文件系统

- **ext4**

  - [fs/ext4/](../fs/ext4/)

- **f2fs**

  - [fs/f2fs/](../fs/f2fs/)

- **squashfs**

  - [fs/squashfs/](../fs/squashfs/)

- **overlayfs**

  - [fs/overlayfs/](../fs/overlayfs/)

- **tmpfs / shmem**

  - [mm/shmem.c](../mm/shmem.c)

- **procfs**

  - [fs/proc/](../fs/proc/)

- **sysfs**

  - [fs/sysfs/](../fs/sysfs/)

- **debugfs**

  - [fs/debugfs/](../fs/debugfs/)

- **configfs**

  - [fs/configfs/](../fs/configfs/)

- **fuse**

  - [fs/fuse/](../fs/fuse/)

- **NFS / 网络文件系统**

  - [fs/nfs/](../fs/nfs/)
  - [fs/nfsd/](../fs/nfsd/)

## 9. 网络与无线

- **网络协议栈**

  - [net/](../net/)
  - [include/net/](../include/net/)

- **IPv4 / IPv6**

  - [net/ipv4/](../net/ipv4/)
  - [net/ipv6/](../net/ipv6/)

- **netfilter / iptables / nftables**

  - [net/netfilter/](../net/netfilter/)
  - [net/ipv4/netfilter/](../net/ipv4/netfilter/)
  - [net/ipv6/netfilter/](../net/ipv6/netfilter/)

- **bridge / VLAN / tunnel**

  - [net/bridge/](../net/bridge/)
  - [net/8021q/](../net/8021q/)
  - [net/ipv4/](../net/ipv4/)
  - [net/ipv6/](../net/ipv6/)

- **Ethernet / GMAC / STMMAC**

  - [drivers/net/ethernet/stmicro/stmmac/](../drivers/net/ethernet/stmicro/stmmac/)
  - [drivers/net/phy/](../drivers/net/phy/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **Wi-Fi 框架**

  - [net/wireless/](../net/wireless/)
  - [net/mac80211/](../net/mac80211/)
  - [drivers/net/wireless/](../drivers/net/wireless/)

- **Rockchip WLAN / Realtek / Broadcom 等无线驱动**

  - [drivers/net/wireless/rockchip_wlan/](../drivers/net/wireless/rockchip_wlan/)
  - [drivers/net/wireless/realtek/](../drivers/net/wireless/realtek/)
  - [drivers/net/wireless/broadcom/](../drivers/net/wireless/broadcom/)
  - [drivers/net/wireless/rtl8189es/](../drivers/net/wireless/rtl8189es/)
  - [drivers/net/wireless/rtl8189fs/](../drivers/net/wireless/rtl8189fs/)
  - [drivers/net/wireless/rtl8723ds/](../drivers/net/wireless/rtl8723ds/)
  - [drivers/net/wireless/rtl8811cu/](../drivers/net/wireless/rtl8811cu/)
  - [drivers/net/wireless/rtl8812au/](../drivers/net/wireless/rtl8812au/)
  - [drivers/net/wireless/rtl88x2bu/](../drivers/net/wireless/rtl88x2bu/)

- **Bluetooth**

  - [net/bluetooth/](../net/bluetooth/)
  - [drivers/bluetooth/](../drivers/bluetooth/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **CAN**

  - [drivers/net/can/](../drivers/net/can/)
  - [drivers/net/can/rockchip/](../drivers/net/can/rockchip/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-can0-m0.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-can0-m0.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-can1-m0.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-can1-m0.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-can1-m1.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-can1-m1.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-can2-m1.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-can2-m1.dts)

## 10. USB、PCIe 与高速接口

- **USB core**

  - [drivers/usb/core/](../drivers/usb/core/)

- **USB host**

  - [drivers/usb/host/](../drivers/usb/host/)
  - [drivers/usb/dwc3/](../drivers/usb/dwc3/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **USB gadget**

  - [drivers/usb/gadget/](../drivers/usb/gadget/)
  - [drivers/usb/dwc3/](../drivers/usb/dwc3/)

- **USB Type-C / TCPM / FUSB302**

  - [drivers/usb/typec/](../drivers/usb/typec/)
  - [drivers/usb/typec/tcpm/fusb302.c](../drivers/usb/typec/tcpm/fusb302.c)

- **USB PHY**

  - [drivers/phy/rockchip/phy-rockchip-inno-usb2.c](../drivers/phy/rockchip/phy-rockchip-inno-usb2.c)
  - [drivers/phy/rockchip/phy-rockchip-inno-usb3.c](../drivers/phy/rockchip/phy-rockchip-inno-usb3.c)
  - [drivers/phy/rockchip/phy-rockchip-usb.c](../drivers/phy/rockchip/phy-rockchip-usb.c)
  - [drivers/phy/rockchip/phy-rockchip-usbdp.c](../drivers/phy/rockchip/phy-rockchip-usbdp.c)

- **PCIe host**

  - [drivers/pci/](../drivers/pci/)
  - [drivers/pci/controller/pcie-rockchip.c](../drivers/pci/controller/pcie-rockchip.c)
  - [drivers/pci/controller/pcie-rockchip-host.c](../drivers/pci/controller/pcie-rockchip-host.c)
  - [drivers/pci/controller/dwc/pcie-dw-rockchip.c](../drivers/pci/controller/dwc/pcie-dw-rockchip.c)

- **PCIe PHY**

  - [drivers/phy/rockchip/phy-rockchip-pcie.c](../drivers/phy/rockchip/phy-rockchip-pcie.c)
  - [drivers/phy/rockchip/phy-rockchip-snps-pcie3.c](../drivers/phy/rockchip/phy-rockchip-snps-pcie3.c)
  - [drivers/phy/rockchip/phy-rockchip-naneng-combphy.c](../drivers/phy/rockchip/phy-rockchip-naneng-combphy.c)

## 11. 低速外设与板载控制

- **I2C**

  - [drivers/i2c/](../drivers/i2c/)
  - [drivers/i2c/busses/](../drivers/i2c/busses/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-i2c*.dts](../arch/arm64/boot/dts/rockchip/overlay/)

- **SPI**

  - [drivers/spi/spi-rockchip.c](../drivers/spi/spi-rockchip.c)
  - [drivers/spi/spi-rockchip-sfc.c](../drivers/spi/spi-rockchip-sfc.c)
  - [drivers/spi/spi-rockchip-slave.c](../drivers/spi/spi-rockchip-slave.c)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-spi*.dts](../arch/arm64/boot/dts/rockchip/overlay/)

- **UART**

  - [drivers/tty/serial/](../drivers/tty/serial/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-uart*.dts](../arch/arm64/boot/dts/rockchip/overlay/)

- **PWM**

  - [drivers/pwm/](../drivers/pwm/)
  - [drivers/pwm/pwm-rockchip.c](../drivers/pwm/pwm-rockchip.c)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-pwm*.dts](../arch/arm64/boot/dts/rockchip/overlay/)

- **SARADC**

  - [drivers/iio/adc/rockchip_saradc.c](../drivers/iio/adc/rockchip_saradc.c)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **LED**

  - [drivers/leds/](../drivers/leds/)
  - [drivers/leds/leds-gpio.c](../drivers/leds/leds-gpio.c)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **PWM fan**

  - [drivers/hwmon/pwm-fan.c](../drivers/hwmon/pwm-fan.c)
  - [drivers/hwmon/](../drivers/hwmon/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **RTC**

  - [drivers/rtc/](../drivers/rtc/)

- **DMA**

  - [drivers/dma/](../drivers/dma/)
  - [drivers/dma/rockchip-dma.c](../drivers/dma/rockchip-dma.c)

## 12. 输入设备

- **input subsystem**

  - [drivers/input/](../drivers/input/)
  - [include/linux/input.h](../include/linux/input.h)

- **ADC keys**

  - [drivers/input/keyboard/adc-keys.c](../drivers/input/keyboard/adc-keys.c)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **GPIO keys**

  - [drivers/input/keyboard/gpio_keys.c](../drivers/input/keyboard/gpio_keys.c)

- **IR remote**

  - [drivers/input/remotectl/](../drivers/input/remotectl/)
  - [drivers/input/remotectl/rockchip_pwm_remotectl.c](../drivers/input/remotectl/rockchip_pwm_remotectl.c)

- **touchscreen**

  - [drivers/input/touchscreen/](../drivers/input/touchscreen/)

- **headset key / headset detect**

  - [drivers/headset_observe/](../drivers/headset_observe/)
  - [drivers/headset_observe/rockchip_headset_core.c](../drivers/headset_observe/rockchip_headset_core.c)

## 13. 显示与图形

- **DRM core**

  - [drivers/gpu/drm/](../drivers/gpu/drm/)
  - [include/drm/](../include/drm/)

- **Rockchip DRM**

  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)

- **VOP / 显示控制器**

  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **HDMI 输出**

  - [drivers/gpu/drm/bridge/synopsys/](../drivers/gpu/drm/bridge/synopsys/)
  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - [drivers/phy/rockchip/phy-rockchip-samsung-hdptx-hdmi.c](../drivers/phy/rockchip/phy-rockchip-samsung-hdptx-hdmi.c)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **DisplayPort / eDP**

  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - [drivers/phy/rockchip/phy-rockchip-dp.c](../drivers/phy/rockchip/phy-rockchip-dp.c)
  - [drivers/phy/rockchip/phy-rockchip-naneng-edp.c](../drivers/phy/rockchip/phy-rockchip-naneng-edp.c)
  - [drivers/phy/rockchip/phy-rockchip-samsung-hdptx.c](../drivers/phy/rockchip/phy-rockchip-samsung-hdptx.c)

- **MIPI DSI**

  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - [drivers/phy/rockchip/phy-rockchip-inno-dsidphy.c](../drivers/phy/rockchip/phy-rockchip-inno-dsidphy.c)
  - [drivers/phy/rockchip/phy-rockchip-samsung-dcphy.c](../drivers/phy/rockchip/phy-rockchip-samsung-dcphy.c)

- **LCD panel / backlight**

  - [drivers/gpu/drm/panel/](../drivers/gpu/drm/panel/)
  - [drivers/video/backlight/](../drivers/video/backlight/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-lcd.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-lcd.dts)

- **framebuffer console**

  - [drivers/video/fbdev/](../drivers/video/fbdev/)
  - [drivers/gpu/drm/drm_fb_helper.c](../drivers/gpu/drm/drm_fb_helper.c)

- **Mali GPU**

  - [drivers/gpu/arm/mali400/](../drivers/gpu/arm/mali400/)
  - [drivers/gpu/arm/midgard/](../drivers/gpu/arm/midgard/)
  - [drivers/gpu/arm/bifrost/](../drivers/gpu/arm/bifrost/)
  - [arch/arm64/configs/rk3588_linux.config](../arch/arm64/configs/rk3588_linux.config)

## 14. 摄像头、ISP 与视频输入

- **V4L2 / media framework**

  - [drivers/media/](../drivers/media/)
  - [include/media/](../include/media/)

- **Rockchip CIF**

  - [drivers/media/platform/rockchip/cif/](../drivers/media/platform/rockchip/cif/)

- **Rockchip ISP / ISP1 / RKISP1**

  - [drivers/media/platform/rockchip/isp/](../drivers/media/platform/rockchip/isp/)
  - [drivers/media/platform/rockchip/isp1/](../drivers/media/platform/rockchip/isp1/)
  - [drivers/media/platform/rockchip/rkisp1/](../drivers/media/platform/rockchip/rkisp1/)

- **Rockchip ISPP**

  - [drivers/media/platform/rockchip/ispp/](../drivers/media/platform/rockchip/ispp/)

- **HDMI-IN**

  - [drivers/media/platform/rockchip/hdmirx/](../drivers/media/platform/rockchip/hdmirx/)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-hdmirx.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-hdmirx.dts)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **MIPI CSI / DPHY**

  - [drivers/phy/rockchip/phy-rockchip-csi2-dphy.c](../drivers/phy/rockchip/phy-rockchip-csi2-dphy.c)
  - [drivers/phy/rockchip/phy-rockchip-csi2-dphy-hw.c](../drivers/phy/rockchip/phy-rockchip-csi2-dphy-hw.c)
  - [drivers/phy/rockchip/phy-rockchip-mipi-rx.c](../drivers/phy/rockchip/phy-rockchip-mipi-rx.c)

- **摄像头 sensor 驱动**

  - [drivers/media/i2c/](../drivers/media/i2c/)
  - [drivers/media/i2c/maxim/](../drivers/media/i2c/maxim/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-max-camera0.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-max-camera0.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-max-camera1.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-max-camera1.dtsi)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-max-camera2.dtsi](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-max-camera2.dtsi)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam0.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam0.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam1.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam1.dts)
  - [arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam2.dts](../arch/arm64/boot/dts/rockchip/overlay/rk3588-opi5ultra-cam2.dts)

- **RGA / 图像加速**

  - [drivers/media/platform/rockchip/rga/](../drivers/media/platform/rockchip/rga/)
  - [drivers/video/rockchip/rga/](../drivers/video/rockchip/rga/)
  - [drivers/video/rockchip/rga2/](../drivers/video/rockchip/rga2/)
  - [drivers/video/rockchip/rga3/](../drivers/video/rockchip/rga3/)

## 15. 视频编解码与多媒体加速

- **Rockchip MPP service**

  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)
  - [drivers/video/rockchip/mpp_osal/](../drivers/video/rockchip/mpp_osal/)

- **RKVDEC / 视频解码**

  - [drivers/staging/media/rkvdec/](../drivers/staging/media/rkvdec/)
  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)

- **RKVENC / 视频编码**

  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)

- **VDPU / VEPU**

  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)

- **AV1 解码**

  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **JPEG 编解码**

  - [drivers/video/rockchip/mpp/](../drivers/video/rockchip/mpp/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **IEP**

  - [drivers/video/rockchip/iep/](../drivers/video/rockchip/iep/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **VPSS**

  - [drivers/media/platform/rockchip/vpss/](../drivers/media/platform/rockchip/vpss/)

## 16. NPU

- **Rockchip RKNPU**

  - [drivers/rknpu/](../drivers/rknpu/)
  - [drivers/rknpu/include/](../drivers/rknpu/include/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

## 17. 音频

- **ALSA / ASoC core**

  - [sound/core/](../sound/core/)
  - [sound/soc/](../sound/soc/)

- **Rockchip ASoC**

  - [sound/soc/rockchip/](../sound/soc/rockchip/)

- **I2S / TDM**

  - [sound/soc/rockchip/](../sound/soc/rockchip/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **PDM**

  - [sound/soc/rockchip/](../sound/soc/rockchip/)

- **SPDIF**

  - [sound/soc/rockchip/](../sound/soc/rockchip/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **HDMI audio**

  - [sound/soc/rockchip/](../sound/soc/rockchip/)
  - [drivers/gpu/drm/rockchip/](../drivers/gpu/drm/rockchip/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **ES8388 codec**

  - [sound/soc/codecs/](../sound/soc/codecs/)
  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dts)

- **headset detect**

  - [drivers/headset_observe/](../drivers/headset_observe/)
  - [sound/soc/rockchip/](../sound/soc/rockchip/)

## 18. 安全、加密与随机数

- **crypto framework**

  - [crypto/](../crypto/)
  - [include/crypto/](../include/crypto/)

- **Rockchip crypto**

  - [drivers/crypto/rockchip/](../drivers/crypto/rockchip/)

- **random / hwrng**

  - [drivers/char/hw_random/](../drivers/char/hw_random/)
  - [drivers/char/hw_random/rockchip-rng.c](../drivers/char/hw_random/rockchip-rng.c)

- **keyring**

  - [security/keys/](../security/keys/)

- **LSM / 安全框架**

  - [security/](../security/)

- **dm-crypt / device mapper**

  - [drivers/md/](../drivers/md/)

## 19. 容器、虚拟化与隔离

- **cgroup**

  - [kernel/cgroup/](../kernel/cgroup/)

- **namespace**

  - [kernel/nsproxy.c](../kernel/nsproxy.c)
  - [kernel/pid_namespace.c](../kernel/pid_namespace.c)
  - [ipc/namespace.c](../ipc/namespace.c)
  - [net/core/net_namespace.c](../net/core/net_namespace.c)

- **Docker 常用内核能力**

  - [kernel/cgroup/](../kernel/cgroup/)
  - [net/netfilter/](../net/netfilter/)
  - [fs/overlayfs/](../fs/overlayfs/)
  - [security/](../security/)

- **KVM**

  - [arch/arm64/kvm/](../arch/arm64/kvm/)
  - [virt/kvm/](../virt/kvm/)

## 20. 调试、追踪与诊断

- **printk / console**

  - [kernel/printk/](../kernel/printk/)
  - [drivers/tty/](../drivers/tty/)

- **dynamic debug**

  - [lib/dynamic_debug.c](../lib/dynamic_debug.c)
  - [Documentation/admin-guide/dynamic-debug-howto.rst](../Documentation/admin-guide/dynamic-debug-howto.rst)

- **ftrace / tracepoint**

  - [kernel/trace/](../kernel/trace/)
  - [include/trace/](../include/trace/)

- **perf**

  - [kernel/events/](../kernel/events/)
  - [tools/perf/](../tools/perf/)

- **kprobe**

  - [kernel/kprobes.c](../kernel/kprobes.c)
  - [arch/arm64/kernel/probes/](../arch/arm64/kernel/probes/)

- **debugfs**

  - [fs/debugfs/](../fs/debugfs/)

- **lockdep / debug objects**

  - [kernel/locking/](../kernel/locking/)
  - [lib/debugobjects.c](../lib/debugobjects.c)

- **panic / oops / crash**

  - [kernel/panic.c](../kernel/panic.c)
  - [arch/arm64/kernel/traps.c](../arch/arm64/kernel/traps.c)

- **Rockchip debug / minidump / FIQ debugger**

  - [drivers/soc/rockchip/rockchip_debug.c](../drivers/soc/rockchip/rockchip_debug.c)
  - [drivers/soc/rockchip/minidump/](../drivers/soc/rockchip/minidump/)
  - [drivers/soc/rockchip/fiq_debugger/](../drivers/soc/rockchip/fiq_debugger/)

## 21. 固件加载与用户态接口

- **firmware loader**

  - [drivers/base/firmware_loader/](../drivers/base/firmware_loader/)

- **uevent / driver core**

  - [drivers/base/](../drivers/base/)
  - [lib/kobject_uevent.c](../lib/kobject_uevent.c)

- **sysfs 设备接口**

  - [fs/sysfs/](../fs/sysfs/)
  - [drivers/base/](../drivers/base/)

- **procfs 状态接口**

  - [fs/proc/](../fs/proc/)

- **ioctl / char device / misc device**

  - [fs/ioctl.c](../fs/ioctl.c)
  - [drivers/char/](../drivers/char/)
  - [drivers/misc/](../drivers/misc/)
  - [drivers/misc/rockchip/](../drivers/misc/rockchip/)

## 22. 文档与阅读入口

- **内核开发流程文档**

  - [Documentation/process/](../Documentation/process/)
  - [Documentation/translations/zh_CN/process/](../Documentation/translations/zh_CN/process/)

- **driver API 文档**

  - [Documentation/driver-api/](../Documentation/driver-api/)
  - [Documentation/translations/zh_CN/driver-api/](../Documentation/translations/zh_CN/driver-api/)

- **device tree 文档**

  - [Documentation/devicetree/](../Documentation/devicetree/)
  - [Documentation/devicetree/bindings/](../Documentation/devicetree/bindings/)

- **subsystem 文档**

  - [Documentation/admin-guide/](../Documentation/admin-guide/)
  - [Documentation/core-api/](../Documentation/core-api/)
  - [Documentation/filesystems/](../Documentation/filesystems/)
  - [Documentation/networking/](../Documentation/networking/)
  - [Documentation/gpu/](../Documentation/gpu/)
  - [Documentation/driver-api/media/](../Documentation/driver-api/media/)
  - [Documentation/userspace-api/media/](../Documentation/userspace-api/media/)
  - [Documentation/admin-guide/media/](../Documentation/admin-guide/media/)
  - [Documentation/sound/](../Documentation/sound/)

## 23. 编译后通常需要带走的内核产物

- **内核镜像**

  - [arch/arm64/boot/Image](../arch/arm64/boot/Image)

- **主 DTB**

  - [arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dtb](../arch/arm64/boot/dts/rockchip/rk3588-orangepi-5-ultra.dtb)

- **可选 DTBO**

  - [arch/arm64/boot/dts/rockchip/overlay/](../arch/arm64/boot/dts/rockchip/overlay/)

- **内核模块**

  - [lib/modules/<kernel-version>/](../lib/)

- **FIT / boot 镜像输入**

  - [boot.its](../boot.its)

## 24. 不属于本内核源码但完整烧录系统通常还缺的内容

- **bootloader**

  - 本仓库通常不包含完整 U-Boot 源码。
  - 常见外部内容：U-Boot、SPL、MiniLoader。

- **ARM Trusted Firmware**

  - 本仓库通常不包含 ATF/BL31 源码和二进制。

- **DDR 初始化二进制**

  - 本仓库通常不包含 DDR init bin。

- **rootfs**

  - 本仓库通常不包含完整 rootfs。
  - 常见外部内容：Debian/Ubuntu/Buildroot/Yocto rootfs。

- **firmware**

  - 本仓库通常不包含完整 `/lib/firmware`。
  - 常见外部内容：Wi-Fi firmware、Bluetooth firmware、GPU firmware、NPU firmware。

- **用户态图形和多媒体库**

  - 本仓库通常不包含完整用户态库。
  - 常见外部内容：Mali 用户态库、RKNPU 用户态库、Rockchip MPP 用户态库、camera HAL、OpenCL/OpenGL/Vulkan 相关库。

- **init 系统和基础用户态**

  - 本仓库通常不包含 systemd、busybox、shell、udev、网络管理工具、apt 包。

- **镜像打包与烧录脚本**

  - 本仓库只包含内核构建相关内容。
  - 完整系统镜像通常还需要分区表、boot.img 打包脚本、rootfs 制作脚本、烧录工具配置。
