# linux-orangepi 阅读发现

## 2026-08-28 RKAIQ/3A 接入边界

- 当前 IQ 文件、学习驱动和 2022 RKAIQ 不属于同一 ABI/JSON 世代。直接运行依次
  暴露 module-info ioctl、ADRC 空指针、无 VCM 的 AF prepare、readback 误判和
  ISP3 params/stats 布局差异。
- 最小兼容后，RKAIQ 可稳定初始化，单摄 online `/dev/video11` 为 30.05 fps，
  停止后 PM 回到 suspended/0。
- 当前真正阻塞是 `/dev/video18` stats 不 dequeue；AE/AWB 只有初始结果，不能
  形成自动闭环。v12 曝光保持 150、gain 16；旧同版本 OV13855 IQ A/B 同样无
  动态 stats，说明问题不只在 OV13850 JSON 转换。
- 私有运行方式必须保留：不覆盖系统 librkaiq、不修改 `/etc/iqfiles`、不自动
  安装 service。完整证据见 `stage6_rkaiq_3a_validation.md`。

## 2026-08-28 RTSP实时PTS与播放器边界

- RKISP实采约30.05fps，不能长期使用`frame_index / 30`作为RTSP实时PTS。该做法
  每分钟让媒体时间超前约95ms，超过30ms jitter窗口后会固定复现积帧、花屏和
  卡顿；客户端重连清零时间基，所以会暂时恢复。
- 服务端queue在75秒调试中保持约33-66ms且无full/leak/overrun；板端本地解码
  稳定，Wi-Fi为5GHz、-41dBm、TX约390Mbps。根因不是queue泄漏或链路容量。
- RTSP应按真实单调时钟生成零基PTS；media重新创建时reset，同一个shared media
  的短暂重连不能把时间轴倒回。
- GStreamer是当前低延迟基准；VLC播放与重连兼容，但默认约400ms，激进时钟参数
  反而约600ms，因此VLC不能承担本项目的延迟验收。
- 手机播放由用户明确移为可选项，不阻塞阶段5完成。

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

## 环境限制结论

- 当前 Codex 已能通过 `wsl -d Ubuntu-22.04` 直接访问、修改和构建
  `/home/wuage2335/linux-orangepi`；此前 UNC/WSL 拒绝属于历史沙盒权限状态，
  不是当前阻塞。
- Windows Explorer 仍可使用
  `\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi`。
- 离线 VHDX/ext4 读取仅是历史 fallback，且曾出现部分文件全零；当前不得再用
  它替代 WSL 内的 `rg/cat/git/make` 作为源码或构建证据。

## 摄像头链路实施状态（2026-08-25）

- 阶段 0“基线验证”、阶段 1“传感器与 DTS”、阶段 2“V4L2 驱动完善”和
  阶段 3“ISP/RGA”、阶段 4“MPP 硬件编码”均已完成；当前进入阶段 5
  “低延迟视频流”。
- 学习驱动 `ov13850_i2c_min.c` 已完成 controls、runtime PM、双模式、
  TRY/ACTIVE、stream lifecycle 和内建实机验证。
- RKISP mainpath 已验证 1920x1080 NV12@30；RGA 文件、实时 copy、实时
  direct-MMAP 均通过 300 帧验收，timeout/drop 为 0。
- 三路径 benchmark 表明 bypass 资源最低；Direct 消除 memcpy 并降低 RSS，
  但总 CPU/RGA 时间存在波动，不能仅凭“零拷贝”名称推断一定更快。
- 当前按需图像变换只有 resize；旋转/色彩转换无业务需求，DMA-BUF 留到后续
  低延迟优化。
- 阶段计划继续只使用阶段编号，不附带周或天等时间估算。

## MPP 阶段 4 最终结论

- 用户态固定使用 Rockchip 官方 MPP `1.1.0`、提交
  `c08762ebfadeb4e986d2fed993bc7a54862d3ebe`；源码、交叉编译产物和 bundle
  都位于忽略的 `mpp/build/`，不污染系统库，也不把整套上游源码提交进仓库。
- 文件路径已验证 H.264/H.265、CBR/VBR、4/8/12 Mbps、GOP 30/60 和运行中
  请求 IDR；官方 MPP decoder 与独立 FFmpeg 均能完整解码目标帧数。
- 实时 copy 路径完成 RKISP 1920x1080 NV12@30 -> MPP H.264 的 300 帧连续
  编码，timeout/drop 均为 0，停流后 sensor runtime PM 回到 suspended/0。
- DMA-BUF 路径通过 `VIDIOC_EXPBUF` 和 `MPP_BUFFER_TYPE_EXT_DMA` 导入同一
  V4L2 buffer；确定性彩条输入下与 copy 路径输出逐字节一致，同时 CPU 由
  8% 降至 3%，因此 DMA-BUF 是阶段 5 的推荐输入路径。
- DMA-BUF 的 `ver_stride` 必须按 V4L2 实际紧凑布局使用 1080；MPP 内部 copy
  buffer 才使用 1088。错误地把外部 buffer 声明成 1088 不一定报错，却会从
  错误 UV 偏移读取数据并产生异常小码流。
- RKVENC regulator/devfreq OPP 启动告警未阻止两个编码核心 probe、官方样例或
  项目编码器工作，当前判定为非阻塞的频率管理风险，后续性能/温升测试继续
  观察。
- Annex-B 裸流没有容器或 RTP 时间戳；即使 MPP 以 30 fps 配置，FFmpeg 仍可能
  猜测为 25 fps。阶段 5 必须在 RTP/RTSP 层显式提供 90 kHz 时间戳，不能用
  裸流探测帧率作为实时链路时序证据。

## `ov13850_i2c_min.c` 阶段 2 最终结论

- 学习 binding 固定为 `learning,ov13850-i2c`，不会抢占正式
  `ovti,ov13850` 驱动。
- `v4l2_i2c_subdev_init()` 后 clientdata 为 `struct v4l2_subdev *`；当前通过
  `ov13850_min_from_client()` 安全回到私有结构，历史错误转换已修复。
- `s_stream` 顺序为 global init -> mode -> control replay -> `0x0100=1`，并在
  完整 streaming 生命周期持有 runtime-PM 引用。
- 已实现 LINK_FREQ、PIXEL_RATE、HBLANK、VBLANK、曝光、模拟增益和测试图；
  VBLANK 会同步更新曝光上限。
- 已实现 2112x1568@30 与 4224x3136@7.5 两种 RAW10 模式、TRY/ACTIVE、最佳
  匹配和切换模式后的 control 范围更新。
- 学习驱动必须内建：模块晚加载会错过 Rockchip CIF/ISP async notifier 组图
  窗口。内建后 media graph、双模式采集和重复启停通过。
- CIF STREAMON 的 ENOMEM 根因是 `enum_frame_interval()` 拒绝 `code=0`，导致
  dummy buffer size 0；仅在非零 code 时校验后修复。
- 实测 2112x1568 为 29.97 fps、4224x3136 为 7.51 fps；停流后 PM 为
  suspended/usage 0，无新增 MIPI fault。`v4l2-compliance` 为 42/43，唯一遗留
  是与正式参考驱动一致的 control event 订阅缺失。
