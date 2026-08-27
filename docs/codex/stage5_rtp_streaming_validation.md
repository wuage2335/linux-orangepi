# Stage 5 RTP Streaming Validation

日期：2026-08-25

## 1. 当前结论

阶段 5 Task 1 的板端/Windows GStreamer 环境检查、Task 2 的 H.264 文件 RTP
基线以及 Task 7 的实时 `V4L2 DMA-BUF -> MPP -> appsrc -> RTP` 均已通过。
Windows 能稳定显示摄像头实时画面；三组同屏计时器样本为 70–100 ms，满足第一版
不高于 200 ms 的验收目标。当前遗留问题是画面偏暗偏绿，已定位到未运行 RKAIQ
3A/IQ，而不是网络或解码链路。

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

## 7. 实时 RTP 结果

实时程序使用以下链路：

```text
OV13850 -> RKISP 1920x1080 NV12@30
-> V4L2 EXPBUF -> MPP EXT_DMA
-> H.264 packet sink -> GstBuffer PTS/DTS
-> appsrc -> h264parse -> rtph264pay -> UDP
```

60 秒实测结果：

```text
mode=dmabuf
frames_in=1800 frames_sent=1800
timeouts=0 dropped=0
packets=1800 idr_frames=60
rtp_clock_rate=90000 timestamp_step=3000
queue_overruns=0
elapsed_s=59.91 loop_fps=30.05
STREAM_RTP_OK
```

Windows 接收端再次协商为 H.264 High、1920x1080@30，并输出
`video/x-raw(memory:D3D11Memory), format=NV12`。用户确认画面来自摄像头实时场景，
运动变化正常。接收日志没有 ERROR/WARNING。停流后 sensor runtime PM 为
`suspended`、usage 为 `0`。

板端重启后 `/dev/video11` 会恢复为 2112x1568；必须先运行
`configure_rkisp_1080p.sh`，读取到 `CONFIGURATION_OK` 后才能启动固定 1080p
sender。该边界由 sender 的格式拒绝检查实机验证。

## 8. 端到端延迟

用户在同一桌面画面中同时显示原始毫秒计时器和 Direct3D11 接收窗口，得到：

| 样本 | 原始计时器 | 接收画面 | 差值 |
| --- | ---: | ---: | ---: |
| 1 | 00:03.58 | 00:03.48 | 100 ms |
| 2 | 00:10.71 | 00:10.64 | 70 ms |
| 3 | 00:25.17 | 00:25.07 | 100 ms |

观测范围为 70–100 ms，三次平均约 90 ms，没有随 60 秒运行持续增加。该方法受显示
刷新、截图时刻和计时器绘制影响，属于端到端近似测量；精确分段延迟仍留给后续
时间戳和抓包测试。

## 9. 暗绿画面边界

实机 controls：

```text
exposure=1536, maximum=1648
analogue_gain=16, minimum=16
test_pattern=Disabled
```

板端存在 `/etc/iqfiles/ov13850_CMK-CT0116_default.json`，但没有 RKAIQ 可执行程序、
软件包、服务或进程。学习驱动也没有 Rockchip `RKMODULE_GET_MODULE_INFO` 等模块
信息 ioctl。当前 ISP 因而没有 AE 自动增益、AWB、CCM 和 Gamma 动态配置。

暗和偏绿在 MPP/RTP 前的 RKISP NV12 输出阶段已经存在；RTP payload、H.264 解码
和 D3D11 输出没有改变颜色。手动提高 analogue gain 可以验证亮度路径，但无法替代
完整 AWB/IQ。

## 10. Task 8 RTP packet timing

板端使用 tcpdump 4.99.1 在 `wlan0` 捕获发往 Windows UDP 5004 的120帧实时流，
再用 Windows tshark 4.6.8离线强制解码为 RTP。

```text
sender frames=120
sender elapsed=4.00 s
sender FPS=30.01
pcap packets=3205
pcap size=3,922,413 bytes
pcap SHA-256=29da5868f3ecf9d63297f1c85164cb2be5245051674c0d3de735df5242a16572
tcpdump kernel drops=0
```

RTP字段统计：

```text
source/destination=192.168.1.10 -> 192.168.1.6:5004
SSRC=0x9fab6acf
payload type=96 only
sequence=1090..4294
sequence gap events=0
missing packets=0
unique timestamp groups=120
marker packets=121
timestamp groups containing marker=120/120
timestamp delta 2999 count=40
timestamp delta 3000 count=40
timestamp delta 3001 count=39
mean timestamp delta=2999.99
```

2999/3000/3001的交替来自微秒整数PTS换算到90kHz时的舍入，平均值等于30fps
要求的3000。第一个timestamp组同时包含codec header与首帧，因此marker packet
总数比timestamp组多1；每个frame timestamp组都至少有一个marker。

原始pcap保存在板端：
`~/ov13850_opi5pro_learning/stage5/task7-live-rtp/measurements/task8-rtp-baseline.pcap`。
因文件为3.8MB且可重新生成，仓库只保存统计、路径和SHA。

## 11. Jitter buffer矩阵

保持sender为GOP30、queue2、8Mbps CBR、900帧，只改变Windows
`rtpjitterbuffer latency`：

| Jitter | 截图延迟 | 平均 | FPS | CPU | Max RSS | Overrun | 实测码率 |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100ms | 200ms；另观察约120ms未截图 | 证据样本200ms | 30.04 | 7% | 28,624KB | 0 | 7.94Mbps |
| 50ms | 140/130ms | 135ms | 30.04 | 6% | 28,604KB | 0 | 7.96Mbps |
| 30ms | 70/100/100ms | 90ms | 30.04 | 7% | 28,596KB | 0 | 7.20Mbps |
| 10ms | 100ms | 100ms | 30.04 | 7% | 28,664KB | 0 | 7.95Mbps |

所有组均900/900帧、0 timeout/drop、PM suspended/0，无停顿、花屏或延迟持续
增加。不同测试拍摄内容不同，实际码率差异不能归因于jitter设置。

推荐 `latency=30ms`：它取得最低延迟范围并保留约一帧网络抖动容错；10ms没有
继续降低实测延迟，却减少迟到包余量。

## 12. GOP和发送queue矩阵

| 配置 | IDR/900帧 | 延迟/恢复 | FPS | CPU | Max RSS | Overrun |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| GOP30, queue2 | 30 | 70–100ms | 30.04 | 7% | 28,596KB | 0 |
| GOP60, queue2 | 15 | 接收重启立即恢复，无花屏 | 30.04 | 6% | 28,600KB | 0 |
| GOP30, queue1 | 30 | 100ms，正常播放 | 30.04 | 6% | 28,632KB | 0 |

GOP60的单次重连成功证明恢复机制可用，但不能推翻其最坏约2秒等待自然IDR的理论
上限。GOP30把自然恢复上限缩短为约1秒。queue1未降低实测延迟；queue2保留一帧
额外调度余量且未造成积帧，因此推荐 `GOP30 + queue2`。

## 13. Queue congestion恢复策略

新增纯C++ `CongestionIdrController`：

- 首次queue overrun立即请求IDR；
- 30帧冷却期内的新overrun合并为pending；
- 冷却结束后补发一次IDR，不对每个drop重复请求；
- overrun计数器重置后重新建立基线。

主机测试覆盖首次、冷却、pending、计数器重置。板端新版sender 300帧黑盒回归：

```text
queue_overruns=0
congestion_events=0
congestion_idr_requests=0
STREAM_RTP_OK
```

当前局域网未真实触发overrun，因此“拥塞时实际恢复画面”的实机证据仍属于后续
网络扰动测试；当前只确认控制逻辑和正常路径没有回归。

## 14. 截图证据

- [jitter100 / 200ms](../stage_5_photo_record/task8/jitter100-200ms.png)
- [jitter50 / 140ms](../stage_5_photo_record/task8/jitter50-140ms.png)
- [jitter50 / 130ms](../stage_5_photo_record/task8/jitter50-130ms.png)
- [jitter30 / 70ms](../stage_5_photo_record/task8/jitter30-70ms.png)
- [jitter30 / 100ms A](../stage_5_photo_record/task8/jitter30-100ms-a.png)
- [jitter30 / 100ms B](../stage_5_photo_record/task8/jitter30-100ms-b.png)
- [jitter10 / 100ms](../stage_5_photo_record/task8/jitter10-100ms.png)
- [queue1 / 100ms](../stage_5_photo_record/task8/queue1-100ms.png)

图片SHA记录在Git对象中；归档前另使用 `sha256sum` 对每个PNG逐项验证。

## 15. Task 8 收口

Task 8 RTP packet timing和参数矩阵完成。推荐基线固定为：

```text
jitter buffer=30ms
GOP=30
sender queue=2
MTU=1200
CBR target=8Mbps
B frames=0
```

Task 9 shared RTSP server和客户端重连已经完成，详细证据见
[`stage5_rtsp_recovery_validation.md`](stage5_rtsp_recovery_validation.md)。暗绿问题
继续作为独立ISP 3A/IQ工作项处理。
