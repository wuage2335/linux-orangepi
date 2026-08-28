# Orange Pi 5 Pro + OV13850 摄像头项目面试讲述文档

## 1. 项目一句话定位

基于 Orange Pi 5 Pro（RK3588S）和 OV13850 MIPI 摄像头，完成从 Linux Sensor
驱动、设备树、RKISP/RGA、MPP 硬件编码到 RTP/RTSP 低延迟传输和 RKAIQ 3A
图像质量调优的端到端摄像头链路，并建立可回滚、可量化、可复现的验证体系。

核心链路：

```text
OV13850 RAW10
-> MIPI CSI-2 / D-PHY
-> CIF / RKISP
-> V4L2 NV12
-> RGA（按需）
-> MPP H.264/H.265
-> RTP/RTSP
-> Windows GStreamer/D3D11
```

主要技术关键词：Linux 6.1、V4L2 Subdev、Media Controller、Device Tree、I2C、
MIPI CSI-2、runtime PM、RKISP、RGA、DMA-BUF、Rockchip MPP、H.264/H.265、
GStreamer、RTP/RTSP、RKAIQ、AE/AWB、交叉编译、串口/SSH 调试。

> 面试时应根据自己的实际参与程度调整“主导、实现、参与”等措辞，不要声称独立
> 完成了自己无法解释的代码。重点讲清需求、定位过程、技术取舍和验证方法。

## 2. 使用 STAR 法则介绍项目

### 2.1 S：Situation，项目背景

项目需要在 RK3588S 开发板上接入 OV13850 CAM2 摄像头，最终形成一条能够演示、
能够量化并可继续扩展 AI 的低延迟视频链路。初始状态存在多层问题：

- OV13850 驱动和 Orange Pi 5 Pro 设备树链路缺少完整实机验证；
- 新内核启动后曾因匹配模块树缺失导致 Wi-Fi 和 SSH 消失；
- Sensor、D-PHY、CSI/CIF、ISP 的 Media Graph 组图存在时序和绑定问题；
- ISP 输出偏暗偏绿，没有运行匹配的 RKAIQ 3A；
- RGA、MPP 和网络传输之间存在内存拷贝、stride、时间戳和队列积压风险；
- RTSP 首版运行约 40～60 秒后会花屏、卡顿并持续累积延迟。

因此，这不是单独写一个摄像头驱动，而是需要打通并验证从内核到用户态、从图像
采集到网络播放的完整链路。

### 2.2 T：Task，我的任务

我的任务可以概括为五点：

1. 完成 OV13850 在 Orange Pi 5 Pro 上的驱动、设备树和双模式采集验证；
2. 将 RAW10 数据经过 RKISP 稳定输出为 1920x1080 NV12，并按需接入 RGA；
3. 使用 Rockchip MPP 完成 H.264/H.265 硬件编码和 DMA-BUF 低拷贝输入；
4. 建立低延迟 RTP/RTSP 传输，解决长期运行中的时间戳漂移和重连问题；
5. 接入 RKAIQ，解决固定曝光、低增益和全局偏绿，并用亮/普通/暗场景量化 AE/AWB。

同时要求每一层都有回滚方案、构建记录、日志、帧文件、SHA 或性能数据，避免用
“画面偶尔出现了”代替工程验收。

### 2.3 A：Action，我采取的行动

#### A1. 驱动和设备树

- 梳理 `compatible -> of_match_table -> probe()`，确认 I2C 地址 `0x10`、2-lane
  MIPI、时钟、GPIO、供电和 endpoint 连接。
- 完善学习驱动的 controls、runtime PM、stream lifecycle、TRY/ACTIVE format
  和 2112x1568、4224x3136 两种模式。
- 修复 `v4l2_i2c_subdev_init()` 后 clientdata 类型变化导致的私有结构体取回风险。
- 定位模块晚加载会错过 Rockchip async notifier 组图窗口，最终将学习驱动内建。
- 定位 CIF STREAMON 的 `ENOMEM`：`enum_frame_interval()` 拒绝 `code=0`，使 dummy
  buffer size 为 0；改为仅在非零 code 时校验。
- 为 RKAIQ 补齐 Rockchip module-info ioctl，并构建了含正式 ioctl 的候选 Image。

#### A2. ISP 和 RGA

- 固化 Sensor、D-PHY、CSI/CIF、RKISP mainpath 的格式和 crop，稳定输出
  1920x1080 NV12。
- 完成文件式 RGA resize、实时 copy 和 direct-MMAP 三种路径。
- 对比后确认 RGA 不是固定必经节点：没有 resize/旋转/格式转换时直接 bypass，
  可以减少排队和资源占用。

#### A3. MPP 编码和低拷贝

- 固定官方 MPP 版本和提交，建立可复现的 aarch64 私有 bundle。
- 实现 H.264/H.265、CBR/VBR、GOP、IDR 请求以及文件/实时编码。
- 使用 `VIDIOC_EXPBUF + MPP_BUFFER_TYPE_EXT_DMA` 导入 V4L2 DMA-BUF。
- 通过确定性彩条定位 DMA-BUF 色度异常：外部 V4L2 NV12 的 `ver_stride` 应为
  1080，而 MPP 内部 copy buffer 使用 1088；错误声明不会立即报错，却会从错误
  UV offset 读取数据。

#### A4. RTP/RTSP 和长期稳定性

- 建立 MPP packet sink、GStreamer appsrc RTP/UDP 和 shared RTSP server。
- 使用有界队列、queue overrun IDR 冷却和客户端重连时的关键帧恢复。
- 对 RTSP 40～60 秒后花屏进行分层排查：服务端 queue 无泄漏，板端本地解码稳定，
  网络带宽也足够，因此排除编码器和 Wi-Fi 容量问题。
- 最终定位为实际采集约 30.05 fps，但旧代码按固定 30 fps 生成 PTS，每分钟累计
  约 95 ms 漂移；改用真实单调时钟生成 PTS 后消除长期积帧。

#### A5. RKAIQ 3A 图像质量

- 固定 RKAIQ v3.0x9.1 和上游提交，所有库、IQ 和兼容工具都放在私有目录，不覆盖
  系统文件。
- 依次解决 module-info `ENOTTY`、AArch64 ioctl 符号扩展、IQ JSON schema、
  ADRC 空指针、无 VCM 的 AF prepare、单摄被误判为 readback、ISP3 UAPI ABI 等问题。
- 通过内核 debug=4 发现前 4 个 stats buffer 已完成，但普通用户创建
  `SCHED_RR` poll 线程失败且旧代码忽略返回值；增加 `SCHED_OTHER` 回退后，
  非 root AE/AWB 真正闭环。
- 建立 test pattern 和真实 bright/normal/dark 场景验收脚本，自动记录 controls、
  Y/U/V、SHA256，并在退出时恢复 sensor controls 和 runtime PM。

### 2.4 R：Result，项目结果

可在面试中使用的已验证结果：

- Sensor：2112x1568 RAW10 为 29.97 fps；4224x3136 RAW10 为 7.51 fps；低分辨率
  连续 5 次启停和高分辨率持续 60 帧通过。
- RKISP/RGA：实时链路 300 帧约 30.04 fps、0 timeout/drop；RGA resize 单次约
  1.63～3.17 ms。
- MPP：H.264/H.265 文件和实时编码均通过；DMA-BUF 路径将该编码进程 CPU 从
  约 8% 降到 3%，并消除约 1.825 ms/帧的显式 copy。
- RTP：1800 帧、30.05 fps、0 timeout/drop/queue overrun；早期同屏延迟样本约
  70～100 ms。
- RTSP：修复固定 PTS 漂移后，五组延迟为 60/70/10/160/60 ms，平均约 72 ms，
  连续运行不再出现单调延迟累积，并支持客户端断开重连。
- RKAIQ：3A 开关在固定彩条下均为 300 帧、9.99 秒、30.04 fps、0 丢帧；3A
  额外占用约 0.9% CPU 和 16 MB RSS。
- 真实场景：bright/normal/dark 的 Y mean 为 123.808/121.255/79.220；随着环境
  变暗，exposure、gain、VBLANK 逐级增加。三帧目视无全局偏绿，暗场只在最大
  增益下有轻微暖紫噪声。
- 所有测试结束后 sensor 均恢复 `runtime_status=suspended`、usage 0，且没有新增
  CSI/ISP/MPP/IOMMU fault。

> 注意：上述 70～100 ms 和平均 72 ms 是接入 3A 前的 Stage 5 低延迟基线。
> 3A 开启后的同屏端到端延迟仍待补录，面试中不能把旧数据冒充最终 3A 延迟。

## 3. 90 秒面试口述稿

这个项目是在 Orange Pi 5 Pro，也就是 RK3588S 平台上接入 OV13850 摄像头，目标
是完成从 Sensor 驱动到 PC 播放的端到端低延迟链路。整个数据路径是 OV13850
RAW10 经过 MIPI、CIF 和 RKISP 输出 NV12，再按需经过 RGA，使用 MPP 做 H.264
或 H.265 硬件编码，最后通过 RTP/RTSP 发送到 Windows。

项目最难的地方不是某一个 API，而是跨层问题的定位。例如驱动晚加载会错过异步
组图窗口；DMA-BUF 的 stride 配错不会报错但会读取错误色度；RTSP 运行一分钟后
花屏也不是网络问题，而是 30.05 fps 实采和固定 30 fps PTS 之间每分钟约 95 ms
的累计漂移。我通过串口、dmesg、media-ctl、v4l2-ctl、内核 debug、码流解码和
量化脚本逐层缩小范围，并改成单调实时时钟。

最终 1080p 链路稳定在约 30 fps，RTP 1800 帧无丢帧，DMA-BUF 将编码进程 CPU
从约 8% 降到 3%。后续又接入 RKAIQ，解决固定曝光和全局偏绿，真实亮、普通、暗
场景的 Y 均值分别约 124、121 和 79。这个项目让我形成了一个比较完整的方法：
先保证每层接口和生命周期正确，再做低拷贝和低延迟优化，并且所有完成结论必须
有日志、帧文件、SHA、性能数据和回滚路径支撑。

## 4. 可写入简历的项目描述

### 4.1 项目描述

基于 RK3588S 与 OV13850 搭建 Linux 摄像头低延迟链路，完成 V4L2 Sensor 驱动、
设备树、RKISP/RGA、MPP H.264/H.265、DMA-BUF、RTP/RTSP 及 RKAIQ AE/AWB
适配，建立覆盖双模式采集、编码解码、长期推流、图像质量、性能和回滚的验证体系。

### 4.2 简历 bullets

- 完善 OV13850 V4L2 Sensor 驱动的 controls、runtime PM、双模式协商和 stream
  lifecycle，实机验证 2112x1568@29.97 fps 与 4224x3136@7.51 fps。
- 打通 RAW10->RKISP->1080p NV12 链路，实现 RGA resize 及 bypass/copy/direct
  对比，300 帧持续处理约 30.04 fps、0 timeout/drop。
- 基于 Rockchip MPP 实现 H.264/H.265 与 V4L2 DMA-BUF 编码，修复 NV12 stride
  导致的色度 offset 问题，将编码进程 CPU 从约 8% 降到 3%。
- 实现 GStreamer RTP/RTSP 低延迟传输和 shared server，定位 30.05 fps 与固定
  30 fps PTS 的累计漂移，改用单调时钟后消除长期花屏和延迟累积。
- 适配 RKAIQ module-info、IQ schema、ISP3 ABI 和非 root stats poll，完成真实
  bright/normal/dark AE/AWB 验收，消除全局偏绿并建立可回滚私有 bundle。

## 5. 简历 bullet 证据映射

| Bullet | 主要证据 | 可信度 | 仍缺材料 |
| --- | --- | --- | --- |
| OV13850驱动、双模式和runtime PM | `ov13850_i2c_min.c`、`task_plan.md`、双模式采集记录 | 高 | 正式Image部署后的module-info ioctl回归 |
| RKISP/RGA 30fps与路径对比 | `rga_v4l2_live_validation.md`、`rga_v4l2_direct_comparison_validation.md` | 高 | 多轮独立benchmark的置信区间 |
| MPP DMA-BUF CPU 8%->3% | `mpp_dmabuf_feasibility.md`、`camera_pipeline_quantitative_results.md` | 高 | 不同动态场景下的多轮CPU统计 |
| RTSP PTS漂移修复与重连 | `live_pts_clock.hpp`、`stage5_rtsp_recovery_validation.md` | 高 | 3A开启后的最终同屏延迟 |
| RKAIQ三场景AE/AWB | `stage6_rkaiq_3a_validation.md`、`stage6_real_scenes_summary.tsv` | 高 | 专业色卡和多色温标定结果 |

## 6. 项目不足以及待优化点

### 6.1 P0：面试中必须主动说明的不足

| 不足 | 当前证据/影响 | 优化方案 |
| --- | --- | --- |
| 3A 开启后的最终同屏延迟尚未补录 | 现有 70～100 ms、平均72 ms来自3A前；不能证明最终画质链路延迟 | 在normal场景用RTSP单调PTS链路重新做同屏计时，记录至少5组平均值、P95和一分钟漂移 |
| 活动内核仍依赖 module-info shim | 已构建含正式ioctl的候选Image，但没有部署；私有运行仍使用LD_PRELOAD兼容 | 受控替换Image，保留旧Image回滚，验证正式ioctl后删除shim |
| RKAIQ 与 IQ 来自不同软件世代 | 通过兼容 patch 和 JSON 转换运行，升级后可能再次出现 ABI/schema 问题 | 获取与Linux 6.1 BSP匹配的RKAIQ/IQ版本，建立版本矩阵和CI ABI检查 |
| 缺少专业 IQ 标定 | 暗场最大gain下仍有暖紫噪声；当前只是功能性AE/AWB，不是量产画质 | 使用灰卡、ColorChecker和多色温光源重新标定AWB、CCM、LSC、Gamma、BLC和降噪 |

### 6.2 P1：性能与稳定性优化

| 不足 | 当前证据/影响 | 优化方案 |
| --- | --- | --- |
| AE 会通过 VBLANK 降低帧率 | normal VBLANK451，dark VBLANK1449；暗场约16.57 fps，编码端不能继续假设固定30fps | 限制最大曝光/VTS，优先增益或启用低照模式；编码和RTP时间戳读取实际帧时间 |
| RGA direct-MMAP 不是真正端到端零拷贝 | Direct路径消除显式memcpy，但RGA仍基于映射地址；性能有调度波动 | 建立V4L2 DMABUF->RGA import->MPP DMABUF的统一buffer pool与fence同步 |
| 长稳测试时长仍有限 | 已完成分钟级和约717秒RTSP测试，但没有24小时3A+编码+网络soak | 增加24小时测试、内存/RSS趋势、温度、DDR带宽、丢帧和自动恢复统计 |
| 网络弱化和故障注入不足 | 已测断开重连和不同jitter，但缺少可控丢包/乱序/带宽压缩 | 使用`tc netem`构造丢包、时延、乱序；验证丢旧帧、IDR恢复和延迟上界 |
| MPP/RTSP指标缺少统一时间线 | 各模块已有局部耗时，但端到端分段延迟未统一 | 为DQBUF、RGA、MPP输入/输出、RTP发送、客户端显示统一采集monotonic timestamp |

### 6.3 P2：工程化和产品化优化

| 不足 | 当前证据/影响 | 优化方案 |
| --- | --- | --- |
| 当前是私有bundle和手工前台运行 | 有校验和与回滚，但还不是产品服务 | 增加systemd unit、最小权限、watchdog、健康检查和日志轮转 |
| 使用较旧vendor kernel和闭源算法库 | Linux 6.1.99基于厂商树，RKAIQ部分算法来自预编译库，维护和升级成本高 | 固定SBOM和上游commit，隔离vendor接口，评估更新BSP或主线化边界 |
| 学习驱动与正式驱动并存 | 独立binding避免抢占，但增加认知和维护成本 | 将已验证修复回合到正式`ov13850.c`，学习驱动只保留最小诊断能力 |
| 暂未接入 RKNPU | 当前只完成视频主链路，阶段7仍是可选项 | 从DMA-BUF分支有界推理队列，使用RGA预处理并保证AI异常不阻塞主链路 |
| 缺少自动化硬件回归平台 | 已有shell测试和验收脚本，但仍依赖人工上电、移动场景和同屏截图 | 增加继电器上电、串口采集、可控光源、测试图卡和自动结果归档 |

## 7. 面试追问建议

### 7.1 这个项目最难的问题是什么？

建议选择“RTSP 时间戳漂移”或“RKAIQ stats poll 线程失败”中的一个深入讲，不要把
所有问题都平铺。回答结构：现象 -> 分层排除 -> 关键证据 -> 根因 -> 修复 -> 回归。

### 7.2 为什么不让 RGA 一直在线？

RGA 是变换单元，不是越多越好。不需要 resize、旋转或颜色转换时，bypass 的 CPU、
RSS 和排队都更低。项目通过对比测试后将 RGA 设计成按需节点。

### 7.3 DMA-BUF 是否等于零拷贝？

不等于。DMA-BUF 只是跨设备共享 buffer 的机制，还要考虑分配方、导入方、stride、
offset、cache coherency、fence 和生命周期。本项目只证明了 V4L2->MPP 编码路径
消除显式 copy；RGA 路径仍有进一步统一 buffer pool 的空间。

### 7.4 如何证明问题真的修好了？

不是只看一帧画面，而是同时检查帧数/FPS、timeout/drop、码流解码、controls、
runtime PM、内核 fault、SHA 和长时间趋势；修改系统文件前保留可启动 Image 和
配置备份，确保每次只改变一个变量。

## 8. 待补充材料

- 3A 开启后 normal 场景的同屏端到端延迟，至少 5 组并给出平均值/P95；
- 一段同时展示 bright/normal/dark 自动收敛和 RTSP 播放的演示视频；
- 24 小时 3A + MPP + RTSP soak 的温度、RSS、丢帧和恢复记录；
- 使用灰卡/ColorChecker/多色温灯源的正式 IQ 标定报告；
- 部署候选 Image、移除 module-info shim 后的启动与回滚证据。

## 9. 证据入口

- `docs/codex/camera_pipeline_quantitative_results.md`
- `docs/codex/stage6_rkaiq_3a_validation.md`
- `docs/codex/stage5_rtsp_recovery_validation.md`
- `docs/codex/project_source_file_index.md`
- `ov13850_opi5pro_learning/orangepi5pro-kernel-troubleshooting.md`
- `drivers/media/i2c/ov13850_i2c_min.c`
- `ov13850_opi5pro_learning/streaming/src/live_pts_clock.hpp`
- `ov13850_opi5pro_learning/rkaiq/patches/0001-ov13850-learning-compat.patch`
