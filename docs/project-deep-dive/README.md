# Orange Pi 5 Pro + OV13850 项目源码导读入口

这套文档面向第一次阅读本项目的学习者。它不按目录机械翻译，而是沿真实运行链路展开：

```text
光线
-> OV13850 RAW10
-> MIPI D-PHY / CSI-2 / CIF
-> RKISP + RKAIQ 3A
-> V4L2 1920x1080 NV12
-> DMA-BUF
-> Rockchip MPP H.264
-> GStreamer RTP/RTSP
-> Windows GStreamer + D3D11
-> 屏幕
```

## 5 分钟导航

1. 先读 [01_overview.md](01_overview.md)，用 5 分钟建立项目边界。
2. 再看 [03_architecture.md](03_architecture.md) 的总图，区分内核、用户态和第三方组件。
3. 打开 [07_code_map.md](07_code_map.md)，严格按 A 级阅读路线看源码。
4. 阅读每个文件时，配合 [05_api_guide.md](05_api_guide.md) 查 class/struct/函数。
5. 最后用 [04_runtime_flow.md](04_runtime_flow.md) 复述启动、单帧、退出与 3A 闭环。

## 文档导航

| 文档 | 主要问题 |
| --- | --- |
| [01_overview.md](01_overview.md) | 项目做什么，已实现什么，边界在哪里 |
| [02_quick_start.md](02_quick_start.md) | 从构建、配置到运行 RTSP 的最短路径 |
| [03_architecture.md](03_architecture.md) | 分层、Pipeline、Producer/Consumer、Client/Server 架构 |
| [04_runtime_flow.md](04_runtime_flow.md) | 启动、单帧、3A、线程、退出与资源生命周期 |
| [05_api_guide.md](05_api_guide.md) | 核心 class/struct/函数/成员变量为什么存在 |
| [06_debug_guide.md](06_debug_guide.md) | 按症状排查不出图、花屏、暗绿、延迟积累等问题 |
| [07_code_map.md](07_code_map.md) | 源码地图、A/B/C/D 分级、文件内部阅读顺序 |
| [08_glossary.md](08_glossary.md) | V4L2、DMA-BUF、MPP、PTS 等缩写和重要变量语义 |
| [09_parameters.md](09_parameters.md) | 分辨率、stride、GOP、码率、queue、Controls 在哪里修改 |
| [10_timing.md](10_timing.md) | 各阶段实测耗时、测量方法和面试表达边界 |

## 一句话抓住代码主线

```text
v4l2_mpp_rtsp_server.cpp::main()
-> run_capture_worker()
-> V4L2Capture::dequeue()
-> MppEncoder::encode_external_frame()
-> EncodedPacketSink::consume()
-> GstRtspServerSink::consume()
-> appsrc ! queue ! h264parse ! rtph264pay
```

它只描述用户态主线。往上追是 RKISP/V4L2/Sensor，往下追是 Linux 网络栈和 Windows 解码显示。

## 面试 90 秒版

> 这是一个基于 RK3588S 和 Linux 6.1 的实时摄像头项目。我从 OV13850 Sensor Subdev 驱动和 DTS 开始，完成了上下电、Runtime PM、双模式、Stream 控制以及曝光、增益、VBLANK 等 Controls。Sensor 输出 RAW10，经 MIPI CSI-2、CIF 和 RKISP 得到 1920x1080 NV12。用户态通过 V4L2 MMAP 取帧，再用 EXPBUF 导出 DMA-BUF，直接导入 Rockchip MPP 做 H.264 硬件编码，最后交给 GStreamer 做 RTP/RTSP 封装和发送。主链路不需要缩放时绕过 RGA，移除了 3.11 MB 的 CPU 整帧拷贝，进程 CPU 实测从 8.2% 降到 3.2%。我还解决了实际 30.05 fps 与固定 30 fps PTS 不一致导致的长时间延迟积累，改成单调时间轴后，RTSP 长会话 21,561 帧、717.58 秒保持 30.05 fps、0 timeout/drop。后续用 RKAIQ 和匹配 IQ 文件完成 AE/AWB 闭环，解决了固定曝光下偏暗偏绿的问题。
