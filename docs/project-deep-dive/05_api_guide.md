# 5. 核心类、结构与函数导读

## 5.1 内核模块：`struct ov13850_min`

**文件**：`drivers/media/i2c/ov13850_i2c_min.c`

### 它是什么

Linux 内核 C 驱动没有 C++ class。`struct ov13850_min` 就是“每个 OV13850 实例的对象”：它把 I2C client、电源资源、当前模式、V4L2 Controls、Subdev 和同步状态放在一起。

### 在系统中的位置

```text
Linux I2C core
-> ov13850_min_probe()
-> struct ov13850_min
-> V4L2 Subdev callbacks
-> I2C register writes
-> OV13850 hardware
```

`probe()` 用 `devm_kzalloc()` 创建它，V4L2/PM/sysfs callback 都通过 `to_ov13850_min()` 或 `ov13850_min_from_client()` 找回它。设备移除后，devm 内存由内核自动释放。

### 重要成员变量

| 成员 | 保存什么 | 赋值/使用/释放 |
| --- | --- | --- |
| `client` | I2C 设备及 `struct device` | `probe()` 赋值；所有寄存器读写使用；由 I2C core 管理 |
| `xvclk` | Sensor 24 MHz 外部时钟 | `devm_clk_get()`；power_on/off 开关 |
| `power_gpio/reset_gpio/pwdn_gpio` | 板级电源、复位、休眠引脚 | 从 DT 获得；上下电时按顺序操作 |
| `supplies[3]` | AVDD/DOVDD/DVDD regulators | bulk get/enable/disable；devm 管理句柄 |
| `powered` | 软件记录 Sensor 当前是否上电 | power_on/off 更新；防止重复操作 |
| `streaming` | 是否已写 `0x0100=1` 并持有 PM 引用 | `s_stream()` 检查和更新 |
| `lock` | 串行化 format/control/stream/sysfs | probe init，remove destroy；control handler 也使用它 |
| `cur_mode` | 当前 ACTIVE 离散 Sensor 模式 | probe 默认 2112x1568；`set_fmt()` 更新；stream-on 读取 |
| `global_regs` | 按 revision 选择的全局寄存器表 | probe 读 revision 后选择；stream-on 重写 |
| `ctrl_handler` | V4L2 Controls 容器 | probe 初始化；Subdev 持有；remove 释放 |
| `exposure/anal_gain/hblank/vblank/test_pattern` | 快速访问各 control 及动态修改范围 | `init_controls()` 创建；`set_fmt()`/`set_ctrl()` 使用 |
| `sd` | 对 V4L2/Media Controller 暴露的 Sensor Subdev | `v4l2_i2c_subdev_init()`；async register/unregister |
| `pad` | Media Graph 的单个 RAW10 source pad | entity pads init/cleanup |
| `fmt` | 当前 ACTIVE media-bus format | probe 默认；`set_fmt()` 更新；`get_fmt()` 返回 |

### 核心函数

#### `ov13850_min_probe()`

**作用**：将 DT 节点描述的硬件变成可被 Media Controller 发现的 V4L2 Sensor Subdev。

**调用者**：Linux I2C core，来自 `ov13850_min_driver.probe`。

**顺序**：检查 adapter → 分配私有对象 → 读 module metadata → 获取 clock/GPIO/regulator → 临时上电 → 读 chip ID/revision → 初始化 Subdev/Controls/Pad → async register → sysfs → Runtime PM。

**为什么错误路径用 `goto`**：C 没有析构函数。每个 label 对应已经取得的最后一类资源，从错误点开始按创建逆序清理，比在每个 `if` 中复制整套释放逻辑更可靠。

#### `ov13850_min_power_on()` / `power_off()`

**作用**：建立/撤销 Sensor 可接受 I2C 且可输出 MIPI 的物理条件。

**顺序**：power GPIO → 24 MHz clock → reset → regulators → release reset → release PWDN → 等待。下电基本反向。

**为什么等待**：Sensor 数据手册对电源、时钟和首次 SCCB/I2C 有稳定时间要求。立即读 ID 会得到 `0x0000`、NACK 或偶发失败。

#### `ov13850_min_s_stream()`

**输入**：`on` 表示开流或停流。

**修改**：`streaming`、Runtime PM usage count、Sensor 寄存器。

**顺序**：加锁 → 幂等检查 → stream-on 时获得 PM 引用 → global/mode/controls → `0x0100=1`；stream-off 时 `0x0100=0` → `pm_runtime_put()`。

**为什么 PM 引用覆盖整个 streaming 期**：只要 Sensor 在出图，就不能被 autosuspend 关闭 clock/电源。

#### `ov13850_min_set_ctrl()`

**调用者**：V4L2 control framework，上层可以是 `v4l2-ctl`、RKAIQ 或 stream-on 的 handler replay。

**映射**：

- Exposure API 值左移 4 位，写 `0x3500..0x3502`。
- Analogue gain 拆成高低字节，写 `0x350a/0x350b`。
- VBLANK + 高度 = VTS，写 `0x380e/0x380f`，同时更新曝光上限。
- Test pattern 写 `0x5e00`。

**为什么断电时不立即唤醒 Sensor**：Control framework 先保存期望值；下次 stream-on 统一 replay，可减少频繁上下电。

#### `ov13850_set_fmt()`

TRY 只修改当前文件句柄的协商草稿；ACTIVE 才修改 `cur_mode`、`fmt` 和 control 范围。流中修改返回 `-EBUSY`，因为一帧内不能中途更换 HTS/VTS/尺寸。

#### `ov13850_min_ioctl()`

当前只处理 `RKMODULE_GET_MODULE_INFO`，返回 sensor/module/lens 名称。RKAIQ 用这组元数据匹配 IQ JSON；它不是读 chip ID。

#### `ov13850_min_runtime_resume()` / `runtime_suspend()`

这两个 callback 将 Linux PM 状态转换到 power_on/off。它们不操作 stream 寄存器；stream 状态由 `s_stream()` 管理。

## 5.2 `class V4L2Capture`

**文件**：`ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp`

### 它是什么

它是 `/dev/video11` 的 RAII 封装。上层不需要反复写 `open/ioctl/mmap/poll/close`，只需要调用 `start()`、`dequeue()`、`requeue()`、`stop()`。

### 在系统中的位置

```text
run_capture_worker() or test main()
-> V4L2Capture
-> Linux V4L2/vb2/RKISP driver
-> /dev/video11 buffers
```

### 重要成员

| 成员 | 含义 |
| --- | --- |
| `fd_` | `open(device, O_RDWR|O_NONBLOCK|O_CLOEXEC)` 得到的 video fd；所有 ioctl/poll 都经过它 |
| `streaming_` | 是否成功 STREAMON；析构时决定是否需要 STREAMOFF |
| `buffers_` | 驱动 buffer 的用户映射、长度、dma-buf fd 和导入 MPP 的 `MppBuffer` |

### `initialize()`

1. `open()` 建立设备句柄。
2. `VIDIOC_QUERYCAP` 确认 multiplanar capture + streaming，避免把错误 video node 当 mainpath。
3. `VIDIOC_G_FMT` 检查 1920x1080 NV12、stride 1920、sizeimage 足够。
4. `VIDIOC_REQBUFS(4)` 请求驱动管理的 vb2 buffer 池。
5. 每个 index 用 `QUERYBUF + mmap` 建立 CPU 虚拟地址。`mmap` 不复制像素。
6. DMA-BUF 模式再用 `EXPBUF` 导出 fd，并以 `MPP_BUFFER_TYPE_EXT_DMA` 导入 MPP。

### `dequeue()`

`poll()` 等待最多 2 秒，然后 `VIDIOC_DQBUF`。返回的 `CapturedFrame` 是借用视图：`data` 只在下一次对同 index `QBUF` 前有效。

### `requeue()`

`VIDIOC_QBUF` 把 index 归还驱动。QBUF 之后 ISP 可以立即覆盖该内存，因此上层必须先结束 CPU/RGA/MPP 对它的访问。

### `cleanup()`

先 STREAMOFF，再 `mpp_buffer_put`，关闭 dma-buf fd，`munmap`，最后关闭 video fd。它是 `noexcept` 式的幂等清理，同时用于构造失败回滚和正常析构。

## 5.3 `struct CapturedFrame`

它不拥有像素内存，只记录：

- `index`：后续 QBUF 需要的 buffer 编号。
- `data`：MMAP 地址，copy 路径使用。
- `sequence`：检测采集丢帧。
- `timestamp_ns`：内核 V4L2 monotonic timestamp，benchmark 用于 SOF 到 DQBUF 分析。
- `timestamp_flags`：标记 timestamp 类型。

它为什么不是 `std::vector<uint8_t>`：如果返回 vector，就会强制每帧复制 3.11 MB，破坏 DMA-BUF 路径。

## 5.4 `class MppEncoder`

**文件**：`ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp`

### 它是什么

它封装 Rockchip MPP encoder context、配置、输入/输出 buffer 和单帧提交。它不知道 Camera 和 Network，只知道“给我 NV12 buffer，我给 sink 编码 packet”。

### 重要成员

| 成员 | 含义 |
| --- | --- |
| `config_` | codec、CBR/VBR、bitrate、GOP、vertical stride |
| `ctx_` | MPP 编码 context，类似一个硬件会话句柄 |
| `mpi_` | MPP 操作函数表，通过它 control/put/get |
| `cfg_` | MPP encoder 配置对象 |
| `group_` | MPP 内部 DRM buffer group |
| `frame_buffer_` | copy 路径的内部 1920x1088 NV12 输入 |
| `packet_buffer_` | 复用的压缩输出 buffer |

### `initialize()`

`mpp_create/init` → block output → 获取 config →设置 prep 格式/stride →设置 30 fps、码率、GOP、QP →设置 H.264/H.265 参数 →每 IDR 重复 header →分配 DRM buffers。

**为什么 output 用 block**：当 `encode_get_packet()` 返回时，可以把当前 V4L2 DMA-BUF 安全 QBUF。如果改成异步，就必须为每个输入跟踪 fence/完成 callback，否则会提前归还 buffer。

### `load_nv12()`

copy 路径把紧凑 1920x1080 NV12 逐行复制到 1920x1088 内部 buffer，使 UV 从目标的 1088 行开始。它还用 `mpp_buffer_sync_begin/end` 处理 CPU 与设备的 cache 可见性。

### `encode_external_frame()`

借用调用者的 `MppBuffer`，不接管所有权。当前传入的是 V4L2 EXPBUF 导出的同一 DMA-BUF。

### `encode_buffer()`

1. 创建轻量 `MppFrame`。
2. 设置 width/height/stride/NV12/PTS/EOS/buffer。
3. 用复用 packet buffer 创建 `MppPacket`。
4. `encode_put_frame()` 提交输入。
5. `encode_get_packet()` 同步取回输出。
6. 从 metadata 判断 IDR，更新 stats。
7. `deliver_packet()` 交给 sink。
8. 释放每帧 frame/packet 描述符。

## 5.5 `EncodedPacketView` 与 `EncodedPacketSink`

**文件**：`encoded_packet_sink.hpp`

`EncodedPacketView` 是非拥有型视图：`data/size` 指向 MPP packet 存储，sink 必须在 `consume()` 返回前用完，不能保存指针以后再用。

| 字段 | 含义 |
| --- | --- |
| `pts_us` | 媒体时间戳；RTSP sink 会映射为真实单调时间 |
| `keyframe` | 是否 IDR/intra，决定 GStreamer delta flag |
| `codec_config` | SPS/PPS 或 VPS/SPS/PPS，不是普通图像帧 |
| `eos` | 码流结束标志 |

`EncodedPacketSink` 只有一个纯虚函数，这是典型的 Strategy/Adapter 边界。

## 5.6 `class GstRtpSink`

**文件**：`gst_rtp_sink.hpp/.cpp`

它将 MPP H.264 packet 复制到 `GstBuffer`，再推入：

```text
appsrc -> bounded leaky queue -> h264parse -> rtph264pay -> udpsink
```

### 成员

- `pipeline_`：拥有整条 GStreamer Pipeline。
- `appsrc_`：C++ 向 Pipeline 注入 packet 的入口。
- `queue_`：编码线程和 GStreamer 网络线程的有界缓冲。
- `bus_`：异步 GStreamer 错误通道。
- `queue_overruns_`：因队列满而丢数据的原子计数。

### `make_gst_buffer()`

这里有一次明确 CPU copy，但复制的是通常几十 KB 的压缩 packet，不是 3.11 MB NV12。然后设置 PTS/DTS/duration 和 keyframe flag。

### 为什么 `queue` 是有界且 leaky

无界队列在网络变慢时会保存越来越多旧画面，表现为延迟累积。`leaky=downstream` 丢最旧数据，优先保持“看到现在”。代价是弱网时可能丢压缩帧，所以上层用 `CongestionIdrController` 请求 IDR 恢复。

## 5.7 `class CongestionIdrController`

输入是 GStreamer queue 的累计 overrun 和当前帧号。它将多个 overrun 合并成一个待处理请求，并使用 `cooldown_frames_` 限制 IDR 频率。

不做 cooldown 的最简单实现是每次 overrun 都请求 IDR；这会让弱网时 IDR 过多，码率更高，反而加重拥塞。

## 5.8 `class GstRtspServerSink`

**文件**：`gst_rtsp_server.hpp/.cpp`

### 它是什么

它是一个 shared RTSP 输出适配器。Camera/MPP 始终只有一套，客户端只是订阅当前 H.264 流。

### 重要成员

| 成员 | 为什么需要 |
| --- | --- |
| `main_loop_` | 运行 GLib 事件循环 |
| `server_` / `factory_` | 管理 RTSP 服务和 `/live` media factory |
| `appsrc_` | 当前 prepared media 中的 packet 入口；可随客户端生命周期出现/消失 |
| `codec_header_` | 缓存 SPS/PPS，新客户端首先获得解码参数 |
| `live_pts_clock_` | 把真实单调时间映射为从 0 开始的会话 PTS |
| `active_clients_` | 无观众时直接丢弃 packet，不积累历史画面 |
| `header_pending_` / `idr_pending_` | 新会话的可解码起点 |
| `state_mutex_` | 保护 capture worker 和 GLib 回调共享的 media/appsrc/时钟/错误状态 |
| `clients_mutex_` | 保护客户端 GObject 引用 vector |
| atomic counters | 无需为统计频繁获取大锁 |

### `consume()`

1. codec config packet 保存到 `codec_header_`，不直接当图像帧发送。
2. 无 `appsrc_` 或无客户端时增加 drop 计数并返回。
3. 锁内获得 appsrc 引用、待发 header 和当前 PTS，锁外执行 push。
4. 先推 header，再推当前 packet。
5. 将 `GST_FLOW_FLUSHING/EOS` 当作客户端生命周期的可恢复丢弃，其他 flow 错误升级为异常。

### `initialize()`

创建 shared factory，内部 Pipeline 是：

```text
appsrc ! queue(max buffers N, leaky downstream)
       ! h264parse(config interval -1)
       ! rtph264pay(config interval 1, MTU N)
```

factory shared 使所有客户端共享同一 media，避免每个客户端重开 Camera。

### `configure_media()` / `clear_media()`

Media prepared 时取得 `appsrc` 和 bus 的引用并重置 PTS；Media unprepared 时先在锁内把共享指针置空，再在锁外断开 signal/释放引用，防止 worker 访问野指针。

## 5.9 `class LivePtsClock`

第一个样本记录 `origin_us_`，后续 PTS = 当前单调时间 - origin。`last_pts_us_` 保证防御性地不倒退。

原先用 `frame_index * 1e6 / 30` 会假设采集精确为 30.00 fps；实际约 30.05 fps，媒体时间与真实时间每分钟漂移约 95 ms，最终超出接收端 jitter window。

## 5.10 RGA 类

### `ImportedBuffer`

保存 `rga_buffer_handle_t`，构造时 `importbuffer_virtualaddr()`，析构时 `releasebuffer_handle()`，禁止复制防止 double release。它是小型 RAII 所有者。

### `VideoCapture`（RGA 实验版）

与共享 `V4L2Capture` 概念一致，但只建立 MMAP，没有 EXPBUF/MPP 导入；`mapped_views()` 为 direct RGA 路径借出固定地址。

### `CopyRgaResizer`

`source_` 和 `output_` 是自有 vector。每帧先从 V4L2 复制到 `source_`，可立即 QBUF，再同步 `imresize()`。所有权简单，但有 3.11 MB CPU copy。

### `DirectRgaResizer`

预先为每个 V4L2 MMAP 地址创建 handle，DQBUF 后按 index 选择 source。它移除输入 CPU copy，但必须等同步 `imresize()` 返回才能 QBUF。输出仍是 `std::vector`，没有直接接入 MPP。

## 5.11 性能测量类

### `BenchmarkConfig`

将 copy/dmabuf、null/rtp、帧数、码率、GOP、MTU、queue 和 CSV 路径放在一个值对象中。这使 benchmark 配置是可复现输入，而不是散落全局变量。

### `SampleSeries`

持有 `vector<double>` 样本，计算 mean/min/max 和 nearest-rank P50/P95/P99。`percentile()` 复制后排序，简单清楚，代价是 O(n log n) 和一份样本内存；对 1500 帧级别足够。

### `TimedPacketSink`

可选持有 `unique_ptr<GstRtpSink>`。null sink 仅计数，RTP sink 计时 `consume()`。`unique_ptr` 表示唯一所有权：有 RTP 时创建，对象析构时自动释放，不需要 `shared_ptr`。

### `FrameTiming` 与 `process_frame()`

`FrameTiming` 是单帧计时记录；`process_frame()` 用同一个 `CLOCK_MONOTONIC` 时域记录 DQ wait、SOF-to-DQ、copy、encode、sink、QBUF 和 post-DQ。这个函数是测量的业务中心。

## 5.12 RKAIQ 接入工具

### `rkmodule_info_probe.c::main()`

打开 Sensor Subdev，调用 `RKMODULE_GET_MODULE_INFO`，确认 sensor/module/lens 字段非空。它将“内核元数据正确”与“RKAIQ 可启动”分开验证。

### `rkmodule_info_preload.c::ioctl()`

这是旧活动内核的受控兼容 shim。只有 request 匹配 module-info 且 `RKAIQ_MODULE_INFO_SHIM=1` 时才返回已知元数据，其余 ioctl 用 `dlsym(RTLD_NEXT, "ioctl")` 转发。

它不应作为最终生产方案：正式方案是驱动自己实现 module-info ioctl。

### `run_rkaiq_local.sh`

检查 bundle/IQ →设置私有 `LD_LIBRARY_PATH` 和受控 preload →转换 IQ 副本 → probe module-info → `exec rkaiq_3A_server`。`exec` 使 shell 进程被 3A server 替换，信号和退出码不多绕一层 wrapper。
