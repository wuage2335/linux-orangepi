# Stage 5 RTP Streaming Validation

日期：2026-08-25

## 1. 当前结论

阶段 5 Task 1 的板端/Windows GStreamer 环境检查完成，Task 2 的 H.264 文件
RTP/UDP 单播基线通过。实时 `V4L2 DMA-BUF -> MPP -> appsrc -> RTP` 尚未开始，
因此本文当前证据只证明插件、网络、RTP caps、H.264 depay 和 Windows 解码路径
兼容，不作为端到端延迟验收结果。

## 2. 环境

### Orange Pi 5 Pro

```text
architecture=aarch64
kernel=6.1.99-opi5pro-livecfg-baseline
gstreamer=1.20.3
video_device=/dev/video11
mpp_device=/dev/mpp_service
```

已验证插件：

```text
appsrc
h264parse
rtph264pay
udpsink
udpsrc
rtpjitterbuffer
rtph264depay
```

开发环境：

```text
gstreamer-1.0=1.20.3
gstreamer-app-1.0=1.20.1
gstreamer-rtsp-server-1.0=1.20.1
g++=11.4.0
```

### Windows

```text
IPv4=192.168.1.6
gstreamer=1.28.6
receiver_port=5004/UDP
jitter_latency=30 ms
```

Windows 插件检查结果：

```text
udpsrc=OK
rtpjitterbuffer=OK
rtph264depay=OK
h264parse=OK
d3d11h264dec=OK
d3d11videosink=OK
avdec_h264=OK
autovideosink=OK
```

接收端最终选择 NVIDIA GeForce RTX 4060 Ti 的 D3D11 硬件解码与 D3D11 显示。

## 3. 输入码流

使用阶段 4 确定性彩条 DMA-BUF 输出：

```text
path=/tmp/mpp-bench-dmabuf.h264
size=29774 bytes
frames=300
sha256=f69d33d682da84db0bbf06b00da11ca62d58045af684730f754e0d0b0b0050a9
```

该码流此前已由官方 MPP decoder 和独立 FFmpeg 完整解码。

## 4. 发送参数

板端发送脚本显式设置：

```text
destination=192.168.1.6:5004
media=video
encoding-name=H264
payload=96
clock-rate=90000
mtu=1200
config-interval=1
framerate=30/1
sync=true
```

发送端协商结果：

```text
video/x-h264
width=1920
height=1080
framerate=30/1
stream-format=byte-stream
alignment=au
profile=high
level=4
```

文件按时钟运行约 9.97 秒后正常 EOS，与 300 帧、30 fps 基线一致。

## 5. Windows 接收结果

接收端依次完成：

```text
application/x-rtp payload=96 clock-rate=90000
-> rtpjitterbuffer
-> rtph264depay
-> h264parse
-> d3d11h264dec
-> d3d11videosink
```

解码输出协商为：

```text
video/x-raw(memory:D3D11Memory)
format=NV12
width=1920
height=1080
framerate=30/1
interlace-mode=progressive
```

接收日志没有 `ERROR` 或 `WARNING`。用户确认 Windows 视频窗口正常显示约 10 秒
彩条画面。

## 6. 安装安全记录

安装板端 GStreamer 开发包时，Ubuntu `initramfs-tools` 触发器重新生成了
`/boot/uInitrd`，并报告 VFAT 不支持 hard-link 备份。部署历史证明当前 baseline
内核此前一直使用 `/boot/uInitrd-6.1.99-ov13850-learning` 启动，因此未重启板子，
先把新生成文件备份到：

```text
/home/orangepi/boot-backups/uInitrd.after-gstreamer-install-20260825_112839
```

随后原子恢复已验证 uInitrd：

```text
sha256=ba7c16a30841788a9237d9c344d31533ed1ff81f3a1eb1289be8f224d78d741b
UINITRD_RESTORE=OK
```

恢复过程中没有重启，正在运行的内核始终是
`6.1.99-opi5pro-livecfg-baseline`。

## 7. 下一步

文件 RTP 基线通过后，下一步先把 `MppEncoder` 从 ostream-only 输出重构为同步
`EncodedPacketSink`，并完整回归阶段 4。随后才能把每个 MPP access unit 连同
PTS/DTS 交给 GStreamer appsrc，建立实时 RTP 链路。
