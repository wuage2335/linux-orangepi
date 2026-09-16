# RK3588 摄像头低延迟链路：技术点学习清单

<!-- camera-status-navigation -->
> 文档同步：2026-09-16。历史数据按原测试条件保留；当前阶段、环境、结果与待办统一见状态入口。
> 最新入口：[Camera 当前状态](codex/CURRENT_STATUS.md)。
<!-- /camera-status-navigation -->

这个项目的核心不是“写一个推流程序”，而是逐层打通并验证：

`Sensor → V4L2 → ISP/RGA → MPP → RTP/RTSP → 播放端 → 性能证据`

建议按下面由浅入深掌握技术点。

## 1. Linux 与硬件基础

- I²C：传感器地址、寄存器读写、芯片 ID。
- GPIO / regulator / clock：复位、掉电、供电时序、24 MHz xvclk。
- MIPI CSI-2：lane 数、RAW10 数据、链路频率。
- Device Tree：`compatible → of_match → probe`，以及 `endpoint/remote-endpoint` 的媒体连接描述。

这些是“设备能否被正确发现”的基础。阶段 0、1 已完成，不应重新铺开，只在问题出现时回查。

## 2. V4L2 Sensor 驱动（已验收的基础能力）

以已验收的 `drivers/media/i2c/ov13850_i2c_min.c` 为学习实现，正式 `ov13850.c` 作对照；学习版已具备Controls、runtime PM、双模式与完整生命周期。

需要掌握：

- Linux I²C 驱动模型：`probe()`、`remove()`、资源申请与错误回滚。
- V4L2 sub-device：`v4l2_i2c_subdev_init()`、media entity、source pad、异步注册。
- 生命周期：上电 → 读 ID → 注册 subdev → stream on/off → 下电。
- 并发与状态：mutex、`streaming` 状态、防止重复启停。
- 寄存器表与模式：global init、mode init、`0x0100` 控制输出。
- 时序参数：HTS/VTS、帧率、曝光上限、link frequency、pixel rate 的互相约束。
- Controls：曝光、模拟增益、VBLANK、HBLANK、测试图。
- runtime PM：设备闲置时掉电；sysfs 调试或流开启时获取 PM 引用，避免对掉电硬件做 I²C 访问。
- 格式协商：TRY/ACTIVE format、mbus code、frame interval、第二种分辨率模式。

回顾阶段 2A 的技术核心：

- 单模块编译、加载/卸载、日志定位。
- `v4l2_i2c_subdev_init()` 会把 I²C `clientdata` 设为 `struct v4l2_subdev *`；后续应通过 `to_ov13850_min()` 回到私有结构体，不能直接把 `i2c_get_clientdata()` 当作 `struct ov13850_min *`。
- 要以首个编译错误为准，不能通过关闭 `-Werror` 绕过问题。

阶段2A–2F已于2026-08-21完成构建与实机验收。不要再根据早期脚手架记录假定这些功能尚缺失。

历史约定（2026-08-06）曾是写完后统一验证；此验证门已完成。当前阶段0–6均已验收，新增测量、限制与下一步以`docs/codex/CURRENT_STATUS.md`为准。

## 3. 媒体管线与图像处理

- 用 `media-ctl -p` 读懂 sensor、DPHY、CIF、RKISP、video node 的真实拓扑。
- 区分 RAW Bayer、ISP 输出的 NV12/YUV。
- 认识 CIF 旁路、ISP 主路径和自路径。
- V4L2 buffer 队列与取帧。
- RGA：缩放、旋转、裁剪、色彩转换；如果 MPP 已可直接吃 NV12，就应绕过 RGA。

目标是明确每一帧在哪个节点、什么格式、是否发生复制。

## 4. RK MPP 硬件编码与低拷贝

- MPP encoder 的输入/输出 buffer 生命周期。
- H.264/H.265 参数：分辨率、FPS、码率、GOP、IDR 请求、B 帧与低延迟权衡。
- 先完成普通内存拷贝版本，再理解 DMA-BUF/DRM buffer 的导入、fd 传递、buffer ownership、同步与释放。
- 时间戳：采集 PTS、编码输出 PTS/DTS、帧顺序和错误恢复。

这一层最关键的能力是：能准确画出每个 buffer 的所有权和拷贝次数。

## 5. RTP/RTSP 低延迟流媒体

- 先用 GStreamer 验证编码码流、网络发送与 PC 解码。
- RTP packetization、RTSP 会话与播放器 buffer。
- 延迟调节：GOP、关闭/限制 B 帧、队列深度、码率控制、丢帧策略。
- 断线重连：重新建立会话、请求关键帧、恢复解码。
- 分段打时间戳：采集、ISP、编码、发送、接收、显示。

## 6. 性能与稳定性工程

- 指标：端到端延迟、FPS、丢帧、码率、CPU、内存、DDR 带宽、温度。
- 对照实验：CPU 路径 vs RGA、内存复制 vs DMA-BUF、启用 RGA vs 直连 MPP。
- 压力场景：长时间推流、连续启停、网络抖动、客户端断开。
- 分层定位：Sensor/I²C → MIPI → media graph → ISP → encoder → network → player。

最重要的工程习惯是：一次只改一个变量，并为每个结论保留构建输出、日志、采集结果或性能数据。

## 推荐学习顺序

`2A 构建与生命周期` → `2B stream` → `2C controls` → `2D runtime PM` → `2E format/mode` → `2F compliance` → `ISP/NV12` → `MPP 拷贝版` → `DMA-BUF` → `GStreamer RTP/RTSP` → `性能对照`。
