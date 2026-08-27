# Orange Pi 5 Pro Low-Latency Streaming

本目录实现阶段 5 的 H.264 RTP/UDP 与后续 RTSP。当前已验证路径：

```text
OV13850 -> RKISP 1920x1080 NV12@30
-> V4L2 DMA-BUF -> Rockchip MPP H.264
-> GStreamer appsrc -> RTP/UDP
-> Windows GStreamer D3D11 decode/display
```

## Environment

板端需要：

```text
GStreamer 1.20.x
gstreamer1.0-plugins-bad
libgstreamer1.0-dev
libgstreamer-plugins-base1.0-dev
libgstrtspserver-1.0-dev
pkg-config
g++
```

环境检查：

```bash
./scripts/check_gstreamer_board.sh
```

Windows 使用官方 GStreamer MSVC x86_64 complete runtime。检查：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\check_gstreamer_windows.ps1
```

## Package And Native Build

在 WSL worktree 中先构建固定 MPP bundle，再生成部署包：

```bash
make -C ../mpp bundle
./scripts/package_streaming_source.sh /tmp/stage5-streaming-dist
```

源码包保留以下相对布局：

```text
ov13850_opi5pro_learning/
├── scripts/configure_rkisp_1080p.sh
├── mpp/src/
├── mpp/build/bundle/official-mpp/
└── streaming/
```

在板端解压后原生编译：

```bash
make -C streaming smoke
make -C streaming test-rtp-sink
make -C streaming rtp
```

程序通过 `$ORIGIN/../lib` 加载随包携带的官方 MPP 1.1.0，不使用系统 MPP 库。

## Reboot Preparation

RKISP mainpath 在重启后会恢复为 2112x1568。每次首次启动 sender 前运行：

```bash
./scripts/configure_rkisp_1080p.sh
```

必须看到：

```text
Format OK: 1920x1080 NV12 stride=1920 size=3110400
CONFIGURATION_OK
```

sender 会拒绝非 1920x1080 NV12 输入，避免把错误 stride 送入 MPP。

## Windows RTP Receiver

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\streaming\scripts\receive_h264_rtp.ps1 `
  -Port 5004 -LatencyMs 30 -Decoder auto
```

`auto` 优先选择 `d3d11h264dec ! d3d11videosink`，不可用时回退
`avdec_h264 ! autovideosink`。

## Live RTP Sender

```bash
./streaming/build/bin/v4l2_mpp_rtp_sender \
  --device /dev/video11 \
  --host 192.168.1.6 \
  --port 5004 \
  --frames 1800 \
  --bitrate 8000000 \
  --gop 30 \
  --mtu 1200 \
  --queue-buffers 2 \
  --mode dmabuf
```

验收输出应包含：

```text
frames_in=1800 frames_sent=1800 timeouts=0 dropped=0
rtp_clock_rate=90000 timestamp_step=3000 queue_overruns=0
STREAM_RTP_OK
```

## Current Result And Boundary

- 1800 帧实时运行约 60 秒，30.05 fps，0 timeout/drop/queue overrun；
- Windows 成功协商 H.264 High 1920x1080@30，并使用 D3D11 NV12 显示；
- 三次同屏计时样本为 100 ms、70 ms、100 ms；
- 当前图像偏暗偏绿来自未运行 RKAIQ 3A/IQ，不是 RTP/MPP 解码问题；
- RTP packet timing和低延迟参数矩阵已完成；本阶段后续工作为RTSP重连。

## Recommended RTP Baseline

Task 8 实测推荐：

```text
receiver jitter buffer = 30 ms
encoder GOP            = 30 frames
sender queue           = 2 buffers, downstream leaky
RTP MTU                = 1200
RTP clock              = 90000
CBR target             = 8 Mbps
B frames               = 0
```

100/50/30/10ms jitter实测延迟分别为约200、130–140、70–100、100ms；所有组
均30.04fps且0 timeout/drop/overrun。10ms没有比30ms继续降低延迟，queue1也没有
带来可见收益，因此保留30ms和queue2的稳定性余量。

GOP60单次重连可立即恢复，但理论最坏要等约2秒自然IDR；GOP30把该上限缩短为
约1秒，适合当前低延迟恢复目标。
