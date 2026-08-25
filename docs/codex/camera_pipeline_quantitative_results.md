# RK3588 摄像头链路量化结果与测量方法

日期：2026-08-25

## 1. 文档目的

本文汇总 Orange Pi 5 Pro（RK3588S）+ OV13850 CAM2 学习项目从 Sensor 驱动、
RKISP、RGA、MPP 到 RTP/UDP 的实机量化结果，并记录每类数据的测量方法、公式和
结论边界。

本文只收录有日志、文件、命令输出或用户同屏画面支持的数据。设计目标、寄存器
理论值和未经重复验证的推测不计作实测结果。

## 2. 基准环境

```text
Board              Orange Pi 5 Pro / RK3588S / 16 GiB
Kernel             6.1.99-opi5pro-livecfg-baseline
Sensor             OV13850 CAM2, RAW10 BGGR, 2-lane CSI-2
Sensor link        300 MHz
Sensor pixel rate  120 MHz
ISP output         /dev/video11, rkisp_mainpath
RGA driver         multicore v1.3.7
librga              official 1.10.6_[3]
MPP                 official 1.1.0, c08762ebf
Board GStreamer     1.20.3
Windows GStreamer   1.28.6
Windows decoder     NVIDIA D3D11 H.264
```

## 3. 总览

| 阶段 | 主要路径 | 核心实测结果 | 当前边界 |
| --- | --- | --- | --- |
| 0–1 | I2C + DTS + media graph | chip ID `0xd850`，revision `0xb2`，CAM2 graph 闭合 | 不含图像质量与吞吐 |
| 2 | Sensor RAW10 | 2112x1568@29.97 fps；4224x3136@7.51 fps | control event 订阅 42/43 中唯一失败 |
| 3 | RKISP + RGA | 1080p NV12 实时约30.04 fps；RGA resize 1.63–3.17 ms/帧 | direct-MMAP 不是 DMA-BUF |
| 4 | MPP H.264/H.265 | 1080p30 实时编码 0 drop；DMA-BUF CPU 3%，copy CPU 8% | 静态场景码率不能代表 RC 收敛 |
| 5 RTP里程碑 | DMA-BUF + MPP + RTP | 1800 帧、30.05 fps、0 drop/overrun；端到端70–100 ms | RTSP/重连尚未完成；画质缺 RKAIQ |

## 4. 阶段 2：OV13850 Sensor 与 V4L2 驱动

### 4.1 模式与吞吐

| 模式 | 单帧文件 | 持续帧率 | 验证长度 |
| --- | ---: | ---: | ---: |
| 2112x1568 RAW10 | 4,415,488 bytes | 29.97 fps | 连续5次启停 |
| 4224x3136 RAW10 | 16,859,136 bytes | 7.51 fps | 持续60帧 |

单帧大小包含 CIF/V4L2 stride 对齐，不能只用 `width*height*10/8` 推断实际文件
字节数。

### 4.2 合规性与生命周期

```text
v4l2-compliance = 42/43
runtime_status  = suspended
runtime_usage   = 0
CRC/ECC/timeout/overflow = 0 new faults
```

`v4l2-compliance` 唯一失败为 control event 订阅；双模式枚举、TRY/ACTIVE、流中
`-EBUSY`、曝光/增益/VBLANK/测试图和 stream lifecycle 均通过。

### 4.3 测量方法

```bash
v4l2-ctl -d <raw-node> \
  --stream-mmap=4 --stream-count=<N> \
  --stream-to=<raw-file> --stream-poll

stat -c '%s %n' <raw-file>
v4l2-compliance -d <sensor-subdev-or-media-node>
cat /sys/bus/i2c/devices/3-0010/power/runtime_status
cat /sys/bus/i2c/devices/3-0010/power/runtime_usage
```

帧率取 `v4l2-ctl` 持续采集输出，不使用 mode table 理论值代替。

## 5. 阶段 3：RKISP 与 RGA

### 5.1 RKISP 输出

```text
Sensor/D-PHY/CSI/CIF  2112x1568 SBGGR10
ISP crop              left=0 top=190 width=2112 height=1188
mainpath              1920x1080 NV12
stride                1920
frame size            3,110,400 bytes
```

NV12 大小验证公式：

```text
1920 * 1080 * 3 / 2 = 3,110,400 bytes
```

重启后 mainpath 会恢复到 2112x1568，必须先运行
`configure_rkisp_1080p.sh` 并确认 `CONFIGURATION_OK`。

### 5.2 文件式 RGA resize

输入为 1920x1080 NV12，输出为 1280x720 NV12：

```text
output size = 1280 * 720 * 3 / 2 = 1,382,400 bytes
warmup      = 5 次
timed runs  = 100 次
```

| Run | RGA total | 平均耗时 | operations/s |
| --- | ---: | ---: | ---: |
| A | 162,863.27 us | 1,628.63 us | 614.01 |
| B | 201,319.06 us | 2,013.19 us | 496.72 |
| C | 245,325.20 us | 2,453.25 us | 407.62 |

观测范围为 1.63–2.45 ms/次，远低于 30 fps 的 33.33 ms 帧周期。该数据只包含
同步 `imresize()`，不包含 Sensor、ISP、文件I/O或显示。

### 5.3 实时 V4L2 copy + RGA

```text
pre_skipped          = 3
processed            = 300
timeouts / dropped   = 0 / 0
copy average         = 731.31 us
RGA average          = 2,592.72 us
copy + RGA           = 3,324.03 us
loop                 = 9.99 s
capture/process FPS  = 30.04
```

### 5.4 bypass/copy/direct 比较

| Path | FPS | Copy avg | RGA avg | 外部CPU | Max RSS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Bypass | 约30 | N/A | N/A | 1% | 12,844 KB |
| Copy | 30.04 | 692.86 us | 2,579.86 us | 5% | 19,564 KB |
| Direct-MMAP | 30.04 | 0 | 3,174.07 us | 5% | 16,380 KB |

另一轮内部统计为 copy 7.11%、direct 2.64%，说明短时 CPU/RGA 调度存在波动。
可稳定得出的结论只有：direct 移除了 memcpy，并比 copy 少 3,184 KB RSS，约减少
16.3%；不能声称每轮总 CPU 或 RGA 时间必然更低。

### 5.5 测量方法

程序内部使用：

```text
std::chrono::steady_clock  测 copy/RGA/loop
getrusage                  测 user/system CPU
V4L2 sequence              统计 dropped frame
poll timeout counter       统计取帧超时
```

外部使用：

```bash
/usr/bin/time -v <program>
stat -c '%s %n' <nv12-output>
sha256sum <nv12-output>
od -An -tu1 -v <plane> | awk '<min/max统计>'
```

## 6. 阶段 4：MPP 硬件编码

### 6.1 官方 MPP 基线

| 输入 | 帧数 | 时间 | 编码吞吐 | 输出 |
| --- | ---: | ---: | ---: | ---: |
| 官方生成1920x1080 NV12 | 30 | 216 ms | 138.35 fps | 337 KiB |
| 实际 RKISP NV12 | 1 | 单帧 | N/A | 2.9 KiB |

官方输出以 Annex-B SPS/PPS/IDR 起始，证明当前 `mpp_service` 与 RKVENC 可用。

### 6.2 项目文件编码器

```text
input              = 1920x1080 compact NV12, 3,110,400 bytes
MPP buffer         = 1920x1088, 3,133,440 bytes
frames             = 300
packets / IDR      = 300 / 5
elapsed            = 1.16 s
encoder throughput = 257.62 fps
```

该吞吐是重复同一内存帧的纯编码能力，不是摄像头实时 FPS。官方 MPP decoder 和
FFmpeg 均完整解码300帧。

参数矩阵还验证了 H.264 CBR 4 Mbps、H.264 VBR 12 Mbps、H.265 CBR 8 Mbps、
GOP30/60和运行中请求 IDR。

### 6.3 实时 copy 路径

```text
frames / packets      = 300 / 300
timeouts / dropped    = 0 / 0
copy average          = 2,027.90 us
MPP average           = 5,122.12 us
loop FPS              = 30.03
encoded bytes         = 3,815,189
duration              ≈ 300 / 30.03 = 9.99 s
measured bitrate      ≈ 3,815,189 * 8 / 9.99 = 3.06 Mbps
```

3.06 Mbps 是该动态场景的实测平均码率；8 Mbps 是 RC target，不应把 target 写成
实测值。

### 6.4 copy 与 DMA-BUF 确定性比较

| Path | FPS | Copy avg | MPP avg | 进程CPU | Max RSS | 输出字节 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Copy | 30.03 | 1,824.80 us | 4,458.99 us | 8% | 18,428 KB | 29,774 |
| DMA-BUF | 30.03 | 0 | 4,836.12 us | 3% | 18,424 KB | 29,774 |

两条彩条码流 SHA-256 完全相同。DMA-BUF 消除约 1.825 ms/帧 copy，进程 CPU
从8%降到3%，相对下降62.5%；本轮 RSS 基本不变。DMA-BUF 有效 UV offset 对应
`ver_stride=1080`，内部 copy buffer 才使用1088。

### 6.5 测量方法

```bash
/usr/bin/time -v ./v4l2_mpp_encoder [--dmabuf] ...
./mpi_dec_test -i output.h264 -t 7 -n 300 -o /dev/null
ffmpeg -i output.h264 -f null -
sha256sum copy.h264 dmabuf.h264
```

程序使用 `steady_clock` 分别统计 copy、同步 MPP packet 返回和完整循环。码率统一
按 `encoded_bytes*8/duration` 重算。

## 7. 阶段 5：RTP/UDP 低延迟里程碑

### 7.1 文件 RTP 基线

```text
input frames       = 300
sender duration    = 9.97 s
payload type       = 96
RTP clock rate     = 90,000 Hz
framerate          = 30/1
Windows output     = D3D11 NV12, 1920x1080@30
```

### 7.2 实时 DMA-BUF -> MPP -> RTP

```text
frames_in / sent   = 1800 / 1800
packets / IDR      = 1800 / 60
timeouts / dropped = 0 / 0
queue overruns     = 0
elapsed            = 59.91 s
loop FPS           = 30.05
encoded bytes      = 57,673,009
measured bitrate   ≈ 57,673,009 * 8 / 59.91 = 7.70 Mbps
RTP timestamp step = 90,000 / 30 = 3,000
```

接收端为 `rtpjitterbuffer latency=30`，随后 H.264 depay/parse、NVIDIA D3D11
硬件解码和 D3D11 显示。停流后 PM 为 suspended/0。

### 7.3 端到端延迟

测量时让 OV13850 拍摄 Windows 毫秒计时器，并在同一桌面截图中同时保留原始
计时器和 Direct3D11 接收窗口：

| 样本 | 原始计时 | 接收画面 | 差值 |
| --- | ---: | ---: | ---: |
| 1 | 03.58 s | 03.48 s | 100 ms |
| 2 | 10.71 s | 10.64 s | 70 ms |
| 3 | 25.17 s | 25.07 s | 100 ms |

```text
minimum = 70 ms
maximum = 100 ms
mean    = (100 + 70 + 100) / 3 = 90 ms
```

已纳入仓库的同屏证据：

- [70 ms 延迟截图](../stage_5_photo_record/image%20copy.png)，1680x1569，
  SHA-256 `8379a6ac7b4df2e178c61e036598bdef0fc262c271a8e80c382ca78902d49abc`；
- [100 ms 延迟截图](../stage_5_photo_record/image.png)，2150x1281，
  SHA-256 `c72b1f19511cd4b612cd814d30cf0a680d9918224867bc08c5c81e3eebb6e5c1`。

60秒运行中没有观察到延迟持续增长，第一版 `<=200 ms` 目标通过。

### 7.4 测量误差与边界

同屏计时法包含 Sensor 曝光、ISP、编码、网络、jitter buffer、解码、显示和桌面
截图，是真正端到端口径；但它仍受两块显示内容的刷新相位、网页计时器绘制和截图
时刻影响，精度约为一个或数个显示刷新周期。后续抓包应验证 RTP sequence、marker
和相邻帧约3000 timestamp增量，但不能用抓包时间替代显示端到端延迟。

当前画面偏暗偏绿来自未运行 RKAIQ 3A/IQ：exposure 为1536/1648，analogue gain
停在最小值16。该问题在 RTP 前的 RKISP NV12 已存在，不计入网络时延失败。

## 8. 数据可信度规则

1. **FPS**：持续帧数除以 `steady_clock` elapsed，或采用 `v4l2-ctl` 连续输出；
2. **码率**：统一使用 `encoded_bytes*8/elapsed_seconds`，不把 RC target 当实测值；
3. **CPU/RSS**：注明使用程序内部 `getrusage` 还是 `/usr/bin/time -v`；
4. **单模块耗时**：只描述被计时调用，不外推为端到端延迟；
5. **实时正确性**：必须同时满足帧数、sequence drop、timeout、解码、PM和fault检查；
6. **确定性比较**：只有输入内容相同才比较 SHA；不同时间的实时画面不要求 hash相同；
7. **阶段状态**：Stage 5 的 RTP 里程碑已通过，但 RTSP 重连尚未完成，整个阶段仍
   标记为进行中。

## 9. 原始证据入口

- `docs/codex/HANDOFF.md`
- `docs/codex/progress.md`
- `docs/codex/rga_nv12_file_resize_validation.md`
- `docs/codex/rga_v4l2_live_validation.md`
- `docs/codex/rga_v4l2_direct_comparison_validation.md`
- `docs/codex/mpp_official_environment_validation.md`
- `docs/codex/mpp_file_encoding_validation.md`
- `docs/codex/mpp_live_encoding_validation.md`
- `docs/codex/mpp_dmabuf_feasibility.md`
- `docs/codex/stage5_rtp_streaming_validation.md`
