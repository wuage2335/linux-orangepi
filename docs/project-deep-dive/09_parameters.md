# 9. 配置、宏、设备树和调参入口

## 9.1 Sensor 常量与寄存器

| 参数 | 当前值 | 作用 | 修改风险 |
| --- | ---: | --- | --- |
| `OV13850_XVCLK_FREQ` | 24 MHz | Sensor 外部时钟 | 必须与 PLL/寄存器表和板级 clock 一致 |
| `OV13850_LINK_FREQ_300MHZ` | 300 MHz | MIPI link frequency menu | 影响 pixel rate/MIPI 时序 |
| `OV13850_LANES` | 2 | CSI-2 data lanes | 必须与 DT endpoint 一致 |
| `OV13850_BITS_PER_SAMPLE` | 10 | RAW10 | 必须与 mbus code 一致 |
| `OV13850_PIXEL_RATE` | 120,000,000 | V4L2 pixel-rate control | 由 link*2*lanes/bits 导出 |
| exposure register | `0x3500..0x3502` | 曝光时间行单位，API 值左移 4 | 不得超过 VTS-16 |
| gain register | `0x350a/0x350b` | 模拟增益 | 值过高会增加噪声 |
| VTS register | `0x380e/0x380f` | 一帧总行数 | 增大可延长曝光，但降低帧率 |
| stream register | `0x0100` | 1 开流，0 software standby | 必须在完整 init/control 之后开启 |
| test pattern | `0x5e00` | 确定性色条 | 用于隔离 Sensor/ISP/格式问题 |

## 9.2 Sensor 模式

| 模式 | HTS | VTS | 默认 exposure | 标称帧率 | 实测 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2112x1568 RAW10 | `0x12c0` | `0x0680` | `0x0600` | 30 fps | 29.97 fps |
| 4224x3136 RAW10 | `0x12c0` | `0x0d00` | `0x0600` | 7.5 fps | 7.51 fps |

新增模式不能只加 width/height，还要有经 Sensor 手册/已知驱动验证的完整寄存器表、HTS/VTS、帧率、link frequency、pixel rate 和 exposure default。

## 9.3 V4L2/RKISP 数据契约

| 参数 | 位置 | 当前值 |
| --- | --- | --- |
| RAW input | `configure_rkisp_1080p.sh` | 2112x1568 SBGGR10 |
| crop | 同上 | left 0, top 190, 2112x1188 |
| mainpath output | 同上 | 1920x1080 NV12 |
| bytes per line | 脚本 + `V4L2Capture` | 1920 |
| frame bytes | 脚本 + `kCaptureInputSize` | 3,110,400 |
| capture buffers | `kCaptureBufferCount` | 4 |
| poll timeout | `kCapturePollTimeoutMs` | 2000 ms |
| warm-up | 各入口 `kSkipFrames` | 3；benchmark 默认 30 |

要支持新分辨率，需要同时修改配置脚本、`V4L2Capture` 合同、MPP width/height/stride/frame size、GStreamer caps 和接收端。不要只改一处常量。

## 9.4 MPP 参数

| 参数 | 默认 | 含义 | 调参建议 |
| --- | ---: | --- | --- |
| codec | H.264 | RTSP 当前只配 H.264 | H.265 只在文件编码路径验证 |
| RC | CBR | 目标码率稳定 | 低延迟网络更易容量规划 |
| bitrate | 8,000,000 bps | 目标 H.264 码率 | 画质与 Wi-Fi 容量权衡 |
| GOP | 30 | 约每 1 秒一个 IDR | 更短恢复快但 IDR 开销高 |
| QP | 10..51 | 质量/压缩比范围 | 需与 RC 一起评估 |
| H.264 profile/level | High / 4.0 | 解码能力和工具兼容 | 改前确认 Windows decoder |
| B frame | 0 | 本项目低延迟路径不用重排帧 | 开 B 帧会引入编码/解码重排缓冲 |
| copy ver stride | 1088 | MPP 内部对齐 buffer | 与逐行 copy 一致 |
| DMA-BUF ver stride | 1080 | V4L2 紧凑 NV12 UV offset | 不可错写 1088 |

## 9.5 RTP/RTSP 参数

| 参数 | 默认/推荐 | 作用 |
| --- | ---: | --- |
| RTP port | 5004 | 直接 UDP 接收端端口 |
| RTSP service | 8554 | RTSP TCP 控制端口 |
| mount | `/live` | RTSP URL 路径 |
| payload type | 96 | 动态 RTP payload type |
| MTU | 1200 | 降低 IP 分片风险 |
| queue buffers | 2 | 小有界队列，网络慢时丢旧保新 |
| receiver jitter | 30 ms | 延迟与抖动容错的实测折中 |
| RTP clock | 90 kHz | H.264 视频 RTP 标准时域 |
| timestamp step | 约 3000 | 30 fps 时 90000/30，实测 2999/3000/3001 |

`queue_buffers` 不是 V4L2 capture buffer 数；前者保存压缩 packet/GstBuffer，后者保存未压缩 NV12 采集帧。

## 9.6 设备树关键项

| 属性 | 作用 |
| --- | --- |
| `reg = <0x10>` | OV13850 的 I2C 地址 |
| `clocks/clock-names = "xvclk"` | Sensor 24 MHz 输入时钟 |
| `reset-gpios/pwdn-gpios` | 上下电时序 |
| `rockchip,camera-module-*` | RKAIQ module-info 与 IQ 匹配 |
| `data-lanes = <1 2>` | 两条 CSI-2 data lane |
| `remote-endpoint` | 建立 Sensor→D-PHY→CSI→CIF→ISP 图 |
| `status` | base dtsi 默认 disabled，overlay 按需启用 |
| `compatible` | 决定 Linux 匹配正式驱动还是学习驱动 |

## 9.7 Kconfig/Kbuild

- `CONFIG_VIDEO_OV13850_I2C_MIN=m/y`：学习驱动编译为模块/内建。
- `drivers/media/i2c/Makefile`：`obj-$(CONFIG_VIDEO_OV13850_I2C_MIN) += ov13850_i2c_min.o`。
- 独立 `O=` 输出目录必须保持 Image、vmlinux、Module.symvers 和 `.ko` 一致。
- `CONFIG_LOCALVERSION` 决定 `uname -r` 和 `/lib/modules/<release>` 名称。

## 9.8 Makefile 和第三方版本

| 模块 | 构建特点 |
| --- | --- |
| RGA | aarch64 交叉编译，使用已固定 librga 1.10.6_[3]，rpath `$ORIGIN/../lib` |
| MPP | 固定官方 MPP 1.1.0，构建 SDK 后构建文件/实时编码器 |
| Streaming | 板端 native `g++` + pkg-config GStreamer，链接私有 MPP bundle |
| Benchmarks | 复用 MPP/V4L2/GStreamer 头和实现，只新增计时记录 |
| RKAIQ | 交叉编译 module-info 工具，上游主体由 fetch script 构建到忽略目录 |

## 9.9 IQ JSON

IQ 不是普通“颜色滤镜”，而是与 Sensor/模组/镜头/ISP 版本匹配的大量标定和算法参数。`prepare_compatible_iq.sh` 只改写私有运行副本，不直接污染 `/etc/iqfiles`。

调画质时先确保算法和文件格式兼容，再做标定/调参；不应通过修改 RTP/MPP 参数来“修复”偏暗偏绿。
