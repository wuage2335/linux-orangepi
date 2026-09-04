# 8. 术语、变量和 C++ 设计用法

## 8.1 Camera 与 Linux 术语

| 术语 | 在本项目中的含义 |
| --- | --- |
| Sensor | OV13850 感光芯片，将光转成 RAW10 数据 |
| RAW10 BGGR | 每像素 10 bit 的 Bayer 原始数据，颜色排列为 BGGR，尚未 demosaic |
| I2C/SCCB | 用于写 Sensor 寄存器的低速控制总线，不传输完整图像 |
| MIPI CSI-2 | Sensor 向 SoC 发送图像 packet 的高速协议 |
| D-PHY | CSI-2 的物理电气层，恢复 lane 上的时钟/位流 |
| CIF/RKCIF | Rockchip Camera Interface，接收 CSI 帧并转发给 ISP/内存 |
| RKISP | Rockchip ISP，将 RAW 做 BLC、demosaic、AWB/CCM/Gamma、YUV 转换和缩放 |
| V4L2 | Linux Video4Linux2 API，用 ioctl 配置并获取视频 |
| Subdev | Media Graph 中不直接输出用户帧的子设备，如 Sensor、D-PHY |
| Media Controller | 用 entity/pad/link 描述 Sensor 到 ISP 的硬件图 |
| vb2 | Videobuf2，内核 V4L2 buffer 队列框架 |
| NV12 | Y 全分辨率 + UV 交错半分辨率的 YUV420，大小为 `W*H*3/2` |
| MPP | Rockchip Media Process Platform，本项目用它调度 RKVENC |
| RGA | Rockchip 2D Graphics Accelerator，用于 resize/rotate/convert |
| RKAIQ | Rockchip ISP 图像质量和 3A 框架 |
| AE/AWB/AF | 自动曝光/自动白平衡/自动对焦；本模组固定焦，AF 禁用 |
| IQ JSON | Sensor/模组/镜头标定与 ISP 调参文件 |
| RTP | 在 UDP 上传输带时间戳的媒体 packet |
| RTSP | 控制媒体会话的客户端/服务端协议；实际媒体仍以 RTP 携带 |
| PTS/DTS | 展示/解码时间戳；本项目无 B 帧时两者相同 |
| GOP | 两个 IDR/I 帧之间的帧组长度 |
| IDR | 可不依赖过去参考帧重新开始解码的 H.264 关键帧 |
| jitter buffer | 接收端用小量缓冲吸收网络包到达时间波动 |

## 8.2 Buffer 与内存术语

### Buffer

不是单纯的“数组”，而是一块有格式、大小、stride、所有者和访问时间的存储。本项目有 V4L2 buffer、MPP frame buffer、MPP packet buffer、GstBuffer 和 RGA buffer。

### `mmap`

把驱动分配的 buffer 页映射到进程虚拟地址。建立映射本身不复制整帧像素。

### DMA-BUF fd

一个可在子系统之间共享同一块底层内存的 Linux 文件描述符。V4L2 `EXPBUF` 产生它，MPP 按 fd 导入。

### stride

内存中相邻两行起点的字节间隔，可以大于可见宽度。copy 路径为 1920x1088 对齐，V4L2 DMA-BUF 实际是紧凑 1920x1080。

### 所有权

V4L2 中 QBUF 后归驱动，DQBUF 后归应用。“有指针”不等于“现在有权读”。

## 8.3 重要程序变量

| 类型/名称 | 不是什么 | 实际保存什么 |
| --- | --- | --- |
| `fd_` | 不是图像 | Linux 内核打开文件表中的索引，代表 `/dev/video11` |
| `MppCtx ctx_` | 不是 C++ 对象所有逻辑 | MPP 内部编码会话 handle |
| `MppApi *mpi_` | 不是一帧数据 | MPP 函数表，类似 C 风格接口对象 |
| `MppBuffer` | 不是必然由 MPP 分配 | MPP 可访问的 buffer handle，可包装外部 DMA-BUF |
| `GstElement *appsrc_` | 不是始终有效的裸指针 | 当前 RTSP media 的 appsrc，持有一个 GObject ref |
| `std::exception_ptr` | 不是错误文本 | 保存 worker 线程原始异常，main join 后重抛 |
| `sequence` | 不是 RTP sequence | V4L2 图像帧序号，用于检测采集丢帧 |
| `pts_us` | 不是 wall-clock 日期 | 相对媒体时间线，单位微秒 |
| `header_pending_` | 不是文件头写入失败 | 新 RTSP media/client 需要补发 SPS/PPS |
| `idr_pending_` | 不是已经有 IDR | 请求 worker 在安全线程内调 MPP control |

## 8.4 为什么使用 RAII

RAII 表示资源与 C++ 对象生命期绑定。`V4L2Capture`、`MppEncoder`、`ImportedBuffer`、`GstRtpSink`、`GstRtspServerSink` 都在析构时释放资源。

最简单的替代是 `main()` 中手写大量 `goto cleanup`，但 C++ 异常从任意深度抛出时容易遗漏。RAII 的代价是需要设计清晰的唯一所有权，并禁止不安全的复制。

## 8.5 `std::unique_ptr` 和 `std::move`

`DirectRgaResizer` 用 `vector<unique_ptr<ImportedBuffer>>` 保存多个禁止复制的 RGA handle 所有者。`std::move(handle)` 将唯一所有权转入 vector，原局部指针变空。

项目没有需要 `std::shared_ptr`的业务对象，因为资源所有者都能被唯一确定。引入 shared ownership 反而会使释放时机难以推理。GObject 则使用自己的 `ref/unref` 引用计数。

## 8.6 `mutex`、`atomic`、callback 和 lambda

### mutex

- Kernel `cam->lock`：format/control/stream/sysfs 会修改同一 Sensor 状态，必须串行。
- `state_mutex_`：worker 和 GLib callback 共享 appsrc/media/PTS/header/error。
- `clients_mutex_`：单独保护 client vector，避免长时间占用主状态锁。

### atomic

stop flag 和统计计数只需要原子读写，不需要将多个字段组成一个一致事务，所以用 `memory_order_relaxed` 可降低不必要的内存顺序约束。

### callback

GStreamer/RTSP 使用 C callback，static callback 从 `user_data` 找回 `this`，再调成员函数。这是 C 库与 C++ 对象绑定的常见方式。

项目主线没有重度使用 lambda，因为 C callback API 需要稳定函数指针，显式 static function 更清楚。

## 8.7 `poll` / `select` / `epoll`

当前只等待一个 video fd，所以使用 `poll()` 足够，并可设 2 秒超时。`select()` 功能相似但 fd set 接口较旧；`epoll` 适合大量 fd 的长期事件循环，这里没有必要。RTSP socket 的多连接事件处理已由 GLib/GStreamer 完成。

## 8.8 常用单位

| 项目 | 单位 |
| --- | --- |
| link frequency | Hz（配置为 300 MHz） |
| pixel rate | pixels/s（配置为 120 MHz） |
| bitrate | bits/s（默认 8,000,000） |
| PTS | 项目内部 us，GStreamer 中 ns |
| benchmark | us 或 ms，文档明确标注 |
| RTP clock | 90 kHz，30 fps 约每帧 3000 tick |
| frame size | bytes，1920x1080 NV12 = 3,110,400 bytes |
