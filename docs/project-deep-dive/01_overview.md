# 1. 项目概览

## 1.1 项目解决什么问题

项目的目标是在 Orange Pi 5 Pro（RK3588S）上把 OV13850 CAM2 从“一颗可以通过 I2C 访问的 Sensor”变成“可稳定采集、自动曝光/白平衡、硬件编码并通过局域网低延迟显示的完整影像系统”。

它同时解决四类问题：

1. **硬件控制**：供电、时钟、复位、I2C 寄存器、MIPI Lane 和设备树 endpoint。
2. **Linux Camera 驱动**：V4L2 Sensor Subdev、模式协商、Controls、Stream 和 Runtime PM。
3. **影像与编码流水线**：RAW10 经 CIF/RKISP 变成 NV12，通过 DMA-BUF 给 MPP 硬件编码。
4. **实时网络与画质**：GStreamer RTP/RTSP、有界队列、关键帧恢复、时间戳修复和 RKAIQ 3A。

## 1.2 典型使用场景

- 嵌入式 Camera Bring-up 和 Sensor 驱动学习。
- 无人机、机器人、智能监控的本地实时视频链路原型。
- RK3588 上 ISP、RGA、RKVENC 硬件加速的性能对比。
- 面试和教学用的 Linux V4L2/DMA-BUF/MPP/RTSP 端到端样例。

## 1.3 技术栈

| 层 | 技术 |
| --- | --- |
| 硬件 | OV13850、RK3588S、2-lane MIPI CSI-2、RKISP v3、RGA、RKVENC |
| 内核 | Linux 6.1.99、I2C、Device Tree/overlay、V4L2 Subdev、Media Controller、vb2、Runtime PM、DMA-BUF/IOMMU |
| 用户态 | C/C++17、V4L2 ioctl、`poll`、`mmap`、RAII、`std::thread`、`mutex`、`atomic` |
| 加速库 | Rockchip MPP 1.1.0、librga 1.10.6_[3]、RKAIQ v3.0x9.1 |
| 流媒体 | GStreamer 1.20.x、appsrc、h264parse、rtph264pay、RTSP Server |
| 构建 | Linux Kbuild/Kconfig、GNU Make、aarch64 交叉编译、pkg-config |
| 验证 | v4l2-ctl、media-ctl、v4l2-compliance、FFmpeg/MPP decoder、pcap、CSV 分位数统计 |

## 1.4 项目采用的架构模式

### 驱动 + 应用

`ov13850_i2c_min.c` 在内核中控制 Sensor；C++ 程序从 `/dev/video11` 获得 RKISP 的 NV12，不直接操作 Sensor 寄存器。

### Pipeline

数据按 Sensor → ISP → V4L2 → MPP → GStreamer → 网络的固定方向流动。每一段只理解自己的输入/输出格式和所有权。

### Producer/Consumer

RKISP 生产 V4L2 buffer，用户态 DQBUF 消费后再 QBUF；MPP 生产编码 packet，`EncodedPacketSink` 消费 packet。

### Client/Server

RTSP 程序在板端持续运行一套 Camera/MPP Pipeline，Windows 客户端可随时连接或断开。

### 事件驱动 + 两线程

RTSP 主线程运行 GLib Main Loop，处理 socket、media 和 client callback；worker 线程串行采集和编码。这是最简单的“硬件所有者 + 网络事件循环”分工。

### 状态机

- Sensor：`powered` / `streaming`。
- V4L2 buffer：driver-owned ↔ user-owned。
- GStreamer：NULL → PLAYING → NULL。
- RTSP media：unprepared → configured → unprepared。

## 1.5 系统边界

**本项目自己实现**：Sensor 学习驱动、配置脚本、V4L2 buffer 封装、MPP 编码封装、RGA 实验、RTP/RTSP 适配、PTS 修复、测量程序和 RKAIQ 兼容接入。

**平台/第三方实现**：CSI/CIF/RKISP 驱动、vb2、MPP 硬件调度、librga、RKAIQ 内部 AE/AWB 算法、GStreamer parser/payloader、Windows D3D11 解码。

RKAIQ 算法源码由构建脚本固定上游提交后放在忽略的 `build/` 目录，因此当前仓库可以证明“如何接入和验证 3A”，但不能从已提交源码逐行分析 AE/AWB 算法内部。

## 1.6 实现状态与证据

### 已实现

- OV13850 双 RAW10 模式、Controls、Runtime PM 与 Stream 生命周期。
  evidence: `drivers/media/i2c/ov13850_i2c_min.c:ov13850_min_s_stream()`、`ov13850_min_set_ctrl()`、`ov13850_min_supported_modes`
- CAM2 上 Sensor → D-PHY → CSI-2 → CIF → RKISP 媒体图。
  evidence: `arch/arm64/boot/dts/rockchip/rk3588s-orangepi-5-pro-camera2.dtsi:ov13850_2`
- 1920x1080 NV12 V4L2 MMAP 采集与 DMA-BUF 导出。
  evidence: `ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp:V4L2Capture::initialize()`
- H.264/H.265 MPP 编码、copy/DMA-BUF 双路径和 IDR 请求。
  evidence: `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp:MppEncoder`
- RTP/UDP 和 shared RTSP，包括有界泄漏队列、新客户端 SPS/PPS + IDR、单调 PTS。
  evidence: `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp:GstRtspServerSink::consume()`、`initialize()`
- RKAIQ 私有 bundle、IQ 兼容转换、module-info 兼容和 AE/AWB 实景验证。
  evidence: `ov13850_opi5pro_learning/rkaiq/scripts/run_rkaiq_local.sh`、`prepare_compatible_iq.sh`
- 分阶段性能计时和 CSV 证据。
  evidence: `ov13850_opi5pro_learning/benchmarks/src/pipeline_stage_benchmark.cpp:process_frame()`

### 部分实现

- RGA 已完成文件缩放和 V4L2 copy/direct-MMAP 实验，但没有形成 `RGA dst DMA-BUF -> MPP` 集成主链路。
  evidence: `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp:DirectRgaResizer`
- RTSP 服务器只配置 H.264；MPP 核心虽支持 H.265，RTSP 下游没有 H.265 payloader/caps 分支。
  evidence: `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp:GstRtspServerSink::initialize()`
- 学习驱动 `v4l2-compliance` 为 42/43，遗留 control event 订阅项。
  evidence: `docs/codex/task_plan.md:阶段 2 验收记录`

### 预留/未落地

- RKNPU AI 感知分支、OSD、跟踪/云台控制。
  evidence: `docs/codex/task_plan.md:阶段 7`
- 手机端播放、同轮 DDR 带宽/温度实测、正常实景 3A 精确同屏延迟。
  evidence: `docs/codex/task_plan.md:阶段 6`
