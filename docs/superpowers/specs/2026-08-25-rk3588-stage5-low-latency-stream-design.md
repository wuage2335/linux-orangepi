# RK3588 Stage 5 Low-Latency Streaming Design

日期：2026-08-25

## 1. 目标

在 Orange Pi 5 Pro 上复用阶段 4 已验证的链路：

```text
OV13850 -> RKISP 1920x1080 NV12@30
-> V4L2 DMA-BUF -> Rockchip MPP H.264
```

将 MPP 输出接入 GStreamer，通过 RTP/UDP 单播发送到 Windows GStreamer，随后在
相同数据路径上增加 RTSP 会话管理。第一版端到端延迟目标不高于 200 ms。

阶段 5 负责视频传输、时间戳、队列和恢复策略；持续时间、温度、DDR 带宽等长稳
测试仍属于阶段 6。

## 2. 已确认边界

- 编码基线：1920x1080、NV12、30 fps、H.264、无 B 帧；
- 编码输入：优先使用阶段 4 已验证的 V4L2 DMA-BUF 路径；
- 第一传输基线：RTP/UDP 单播；
- 第一接收端：Windows 主机上的 GStreamer；
- 第二接收端：VLC 或 FFmpeg，仅用于交叉验证；
- RTP 基线通过后，再增加 RTSP；
- 端到端延迟由 OV13850 拍摄毫秒计时器，用户使用手机同时记录原始计时器和
  Windows 接收画面；
- 验收目标：1080p30、持续 300 帧以上、无持续积帧、接收端重启后恢复、端到端
  延迟不高于 200 ms。

## 3. 方案选择

采用“MPP 编码器 + GStreamer appsrc”。不使用纯命令行管道作为最终实现，也不
自行实现 RFC 6184 RTP/H.264 packetizer。

原因：

- 纯命令行适合验证环境，但难以可靠保留每个 MPP access unit 的边界和 PTS；
- 自研 RTP packetizer 需要正确处理 NAL、FU-A、MTU、marker、sequence、SSRC、
  SPS/PPS 和丢包恢复，协议风险会掩盖摄像头链路学习目标；
- GStreamer 已提供 `h264parse`、`rtph264pay`、`rtpjitterbuffer` 和 RTSP server，
  同时 `appsrc` 允许项目显式提交每个 MPP packet 与时间戳。

## 4. 总体架构

### 4.1 RTP/UDP

```text
/dev/video11
-> V4L2 DQBUF
-> imported DMA-BUF
-> MPP H.264 access unit
-> synchronous packet callback
-> GstBuffer(PTS/DTS/duration)
-> appsrc
-> bounded leaky queue
-> h264parse
-> rtph264pay
-> udpsink
-> LAN
-> Windows udpsrc
-> rtpjitterbuffer
-> rtph264depay
-> H.264 decoder
-> low-latency video sink
```

### 4.2 RTSP

```text
same MPP packet source
-> shared gst-rtsp-server media factory
-> appsrc -> h264parse -> rtph264pay
-> RTSP client session
```

RTSP 只增加会话描述、客户端连接和重连管理，不重新实现采集或编码。

## 5. 代码边界

### 5.1 MPP packet 输出重构

当前 `MppEncoder` 直接把 packet 写入 `std::ostream`。阶段 5 将输出改为同步 sink
接口，sink 在回调返回前消费 packet 数据：

```text
EncodedPacketView
- data / size
- pts_us
- keyframe
- codec_config
- eos
```

实现两个 sink：

- file sink：保持阶段 4 文件和实时落盘行为；
- GStreamer sink：复制编码 packet 到 `GstBuffer`，设置时间戳后交给 appsrc。

`MppPacket` 的内存仍由 MPP 管理，sink 不允许在回调返回后保存裸指针。这样可以
清晰表达所有权，并防止 packet deinit 后的悬空访问。

### 5.2 GStreamer bridge

新增独立实时发送程序，不把网络代码塞进阶段 4 的验证工具。发送程序复用
`VideoCapture`、DMA-BUF import 和 `MppEncoder`，但将 packet 交给 appsrc。

第一版 pipeline：

```text
appsrc is-live=true format=time block=false
-> queue max-size-buffers=2 leaky=downstream
-> h264parse
-> rtph264pay pt=96 mtu=1200 config-interval=1
-> udpsink sync=false async=false
```

具体属性必须通过板端 `gst-inspect-1.0` 确认后固定，不能仅凭桌面 GStreamer 文档
假设插件版本和属性存在。

### 5.3 时间戳

30 fps 下每帧：

```text
GstBuffer PTS      = frame_index * 1,000,000,000 / 30 ns
GstBuffer DTS      = PTS
GstBuffer duration = 1,000,000,000 / 30 ns
RTP clock-rate     = 90000
RTP timestamp step = 90000 / 30 = 3000
```

程序设置 GstBuffer 时间；`rtph264pay` 根据 GStreamer running-time 转换为 90 kHz
RTP timestamp。程序不直接伪造 RTP header。使用抓包或接收端统计验证 clock-rate
与相邻帧约 3000 的 timestamp 增量。

MPP 的 codec header 标记为 header/config 数据；普通 packet 按 access unit 提交。
`h264parse` 和 payloader 负责 Annex-B NAL 解析与 SPS/PPS 周期重发。

## 6. 队列与低延迟策略

- 采集侧继续使用 4 个 V4L2 buffer；
- MPP 同步返回 packet 后再 QBUF 对应 DMA-BUF；
- appsrc 不允许无限阻塞采集线程；
- GStreamer 队列最多保留 2 个 encoded buffer，并设置 downstream leaky；
- 队列拥塞时丢弃旧的编码帧，避免延迟随时间增长；
- 接收端 jitter buffer 从小值开始测试，启用超时丢弃；
- 视频 sink 关闭额外同步等待或使用最低可用缓存；
- H.264 保持无 B 帧；第一版 GOP 30，保证新接收端最多约 1 秒等到自然 IDR；
- 新 RTSP 客户端连接或拥塞恢复时请求 MPP 产生 IDR，并确保 SPS/PPS 可用。

丢弃 P 帧后可能在下一个 IDR 前出现不可解码区间，因此程序需要统计 queue drop，
并在发生拥塞或新客户端加入时请求 IDR，而不是静默持续丢帧。

## 7. 接收端基线

Windows pipeline 的逻辑组成固定为：

```text
udpsrc with explicit application/x-rtp caps
-> rtpjitterbuffer
-> rtph264depay
-> h264parse
-> available hardware or software H.264 decoder
-> low-latency video sink
```

实际 decoder/sink 名称以 Windows `gst-inspect-1.0` 为准。优先 D3D11 硬件路径，
不可用时使用软件 decoder 作为功能基线。计划必须保存发送端和接收端完整命令，
不能依赖自动类型猜测。

## 8. RTSP 与恢复

- RTP/UDP 基线通过后才引入 `gst-rtsp-server`；
- RTSP factory 使用 shared media，避免每个客户端重复打开摄像头和 MPP；
- 新客户端连接时请求 IDR；
- 客户端退出不停止采集线程，最后一个客户端退出后的资源策略在实机测量后确定；
- 接收端重启后应在下一个 IDR/SPS/PPS 后恢复；
- GStreamer bus 的 ERROR、EOS、WARNING 必须转成明确退出码和日志；
- UDP 本身没有连接状态，“恢复”定义为接收端重新启动后重新获得画面；RTSP 恢复
  还包括会话断开和重新建立。

## 9. 环境和构建策略

先检查板端与 Windows：

- GStreamer 版本；
- appsrc、h264parse、rtph264pay、udpsink、udpsrc、rtpjitterbuffer、rtph264depay；
- Windows 可用 decoder 和 video sink；
- 板端 `g++`、`pkg-config`、GStreamer development headers；
- `gst-rtsp-server` runtime 和 development package。

阶段 5 不把整套 GStreamer vendoring 到仓库。GStreamer bridge 优先在板端使用发行版
开发包装进行 native build；如果环境缺失，再记录包名和安装步骤。MPP 仍使用阶段 4
固定 bundle，不能改用来源不明的系统 MPP 插件。

## 10. 错误处理和可观测性

发送程序至少记录：

- V4L2 sequence、capture frame count、timeout、drop；
- MPP packet count、IDR count、encoded bytes、encode time；
- GstBuffer PTS/DTS/duration；
- appsrc push 返回值、queue drop、GStreamer bus error；
- 发送帧数、发送字节数、运行时间和平均码率；
- 退出后 sensor runtime PM 状态和新增 CIF/ISP/MPP/IOMMU fault。

正常退出和错误退出都必须停止 V4L2、释放 imported MppBuffer、关闭 EXPBUF fd、
unref GStreamer 对象，并使 sensor PM 回到 suspended/usage 0。

## 11. 验证顺序

1. 环境清单与插件检查；
2. 已有 H.264 文件的 RTP/UDP loopback；
3. 板到 Windows 的文件 RTP；
4. MPP sink 重构后重新运行阶段 4 文件/参数/实时回归；
5. 实时 DMA-BUF -> MPP -> RTP，先 300 帧再持续运行；
6. 抓包验证 payload type、sequence、marker、90 kHz clock 和 timestamp；
7. 调整 jitter buffer、queue、GOP 和 sink 缓冲；
8. 用户用手机执行毫秒计时器端到端测量；
9. 增加 RTSP，验证接收端重启和会话重连；
10. 汇总 CPU、RSS、FPS、drop、码率、延迟与内核日志。

## 12. 验收标准

- Windows GStreamer 稳定显示 1920x1080@30；
- 发送端连续 300 帧以上，无 V4L2 timeout，无持续 queue accumulation；
- RTP payload type、H.264 depay 和 90 kHz 时间基正确；
- 端到端延迟重复测量不高于 200 ms，并记录多次测量范围；
- 接收端退出再启动后能够在关键帧处恢复；
- RTSP 客户端断开并重连后恢复画面；
- 退出后 sensor PM 为 suspended/usage 0；
- 无新增 CIF、ISP、MPP、RKVENC、MMU 或 IOMMU fault；
- 阶段 4 文件编码、H.265、实时 copy 和 DMA-BUF 回归不被破坏。

## 13. 不在本阶段实现

- 自研 RTP/H.264 packetizer 或完整 RTSP 协议栈；
- 公网穿透、鉴权、TLS、WebRTC、SRT；
- 多路摄像头、多播或多码率自适应；
- AI 推理和 OSD；
- 阶段 6 的长时间温升、DDR 带宽与系统稳定性矩阵。
