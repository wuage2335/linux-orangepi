# RK3588 摄像头低延迟链路项目计划

## 项目目标

在 RK3588 平台上完成一条可复现、可测量、可演示的摄像头链路：

`OV13850/MIPI -> CSI/DPHY -> CIF/ISP -> V4L2 -> RGA（按需） -> MPP -> RTP/RTSP -> PC 播放`

第一版以 `1080p30 + H.264 + RTP/RTSP` 为基线，重点验证驱动正确性、硬件编解码、低拷贝数据传递、端到端延迟和长时间稳定性。

## 最高优先级协作原则

- 这是用户的学习项目，用户是主要实践者，Codex 的角色是导师、结对伙伴和评审者。
- 默认流程是：讲解原理 -> 拆分小任务 -> 用户动手 -> 检查结果 -> 引导调试 -> 总结复盘。
- 未经用户明确授权，Codex 不得一次性代写完整阶段、完整功能或大批量代码。
- Codex 可以主动进行只读分析、解释源码、制定步骤、给出局部示例、设计验证命令和审查用户结果。
- 需要修改代码时，先说明修改目标、涉及文件和学习重点，再让用户选择亲自实现或明确授权 Codex 修改。
- 只有用户明确提出“直接实现”“直接修改”“帮我完成”等授权时，Codex 才可以代为实现；仍应分小步说明并保留验证过程。
- 不因 Codex 生成了代码就将阶段标记完成；阶段完成必须以用户理解、实机结果和验收证据为依据。

## 阶段状态

- [x] 阶段 0：基线验证
- [x] 阶段 1：传感器与 DTS
- [x] 阶段 2：V4L2 驱动完善
- [x] 阶段 3：ISP 与 RGA 图像处理
- [x] 阶段 4：MPP 硬件编码
- [ ] 阶段 5：低延迟视频流（当前阶段）
- [ ] 阶段 6：性能测量与稳定性
- [ ] 阶段 7：AI 感知与业务扩展（可选）

## 阶段 0：基线验证（已完成）

目标：确认摄像头能够被系统发现，媒体拓扑完整，并能从正确的视频节点采集图像。

完成标准：

- 能通过日志确认 sensor probe 状态。
- 能说明 `media-ctl -p` 中 sensor、DPHY、CSI/CIF、ISP 和 video node 的连接关系。
- 能使用 `v4l2-ctl` 完成稳定采集，并确认格式、分辨率和帧率。

## 阶段 1：传感器与 DTS（已完成）

目标：掌握 OV13850 的供电、时钟、复位、I2C、MIPI Lane、endpoint 和 overlay 配置。

完成标准：

- 能独立解释并修改 sensor 到 ISP 的设备树链路。
- 能定位 `compatible -> of_device_id -> probe` 的驱动匹配过程。
- 能根据启动日志判断电源、时钟、I2C、MIPI 或 endpoint 配置问题。

## 阶段 2：V4L2 驱动完善（已完成）

目标：以正式 `ov13850.c` 为主线，理解并验证一个完整 V4L2 sensor subdev 驱动。

任务：

- 梳理 `probe`、上电/下电、stream on/off、模式切换和 runtime PM 调用链。
- 核对 mode table、HTS/VTS、link frequency、pixel rate 和曝光范围之间的关系。
- 验证曝光、模拟增益、VBLANK、测试图等 V4L2 controls。
- 完成一次可验证的驱动改动，例如增加模式、修正控制范围或补充错误处理。
- 使用 `v4l2-compliance`、连续启停和持续采集验证驱动行为。

完成标准：驱动改动有明确需求、代码提交、测试命令、日志和采集结果，连续启停及持续采集无明显错误。

阶段 2 学习子步骤：

- [x] 2A：隔离 binding、V4L2 control/PM 状态与同一 `O=` 构建基线。
- [x] 2B：实现 global init -> mode -> controls -> stream on/off。
- [x] 2C：实现 link frequency、pixel rate、HBLANK、VBLANK、曝光、模拟增益和测试图。
- [x] 2D：实现 runtime PM，并覆盖 stream 与 sysfs 调试访问。
- [x] 2E：实现 TRY/ACTIVE、两个 RAW10 模式、frame interval 与 endpoint 配置。
- [x] 2F：完成内建启动、双模式采集、重复启停、持续帧率和日志检查。

阶段 2 验收记录（2026-08-21）：2112x1568 为 29.97 fps，4224x3136 为
7.51 fps；两种 RAW10 模式均成功采帧。`v4l2-compliance` 42/43，唯一遗留为
control event 订阅。详细证据见 `HANDOFF.md` 和 `progress.md`。

## 阶段 3：ISP 与 RGA 图像处理（已完成）

目标：理解 RAW 数据经过 RKISP 后生成 NV12/YUV 的过程，并按需使用 RGA 完成缩放、旋转或色彩转换。

任务：

- 区分 CIF 旁路、ISP 主路径和自路径视频节点。
- 固定 ISP 输出为适合 MPP 的 NV12 格式。
- 完成 V4L2 取帧与 RGA resize/rotate/convert 示例。
- 比较 CPU 处理与 RGA 处理的占用和吞吐。

完成标准：稳定输出指定分辨率的 NV12 帧；需要变换时走 RGA，不需要变换时允许绕过 RGA。

阶段 3 验收记录（2026-08-25）：

- [x] 区分 CIF RAW、RKISP mainpath/selfpath，并固定 1920x1080 NV12@30。
- [x] 官方 librga 1.10.6_[3] 文件式 resize 验证通过。
- [x] 实时 copy 路径处理 300 帧，30.04 fps、0 timeout/drop。
- [x] 实时 direct-MMAP 路径处理 300 帧，30.04 fps、0 timeout/drop。
- [x] 完成 bypass/copy/direct CPU、吞吐和 RSS 对比；Direct 确定移除 memcpy
  并降低 RSS，但总 CPU/RGA 耗时存在波动，不声称每轮必然更快。
- [x] 输出、runtime PM 和 CIF/ISP/RGA/MMU/IOMMU fault 检查通过。

当前业务只需要 resize；旋转和色彩转换不作为无需求功能实现。DMA-BUF 留到
后续低延迟优化。详细证据见 `rga_nv12_file_resize_validation.md`、
`rga_v4l2_live_validation.md` 和 `rga_v4l2_direct_comparison_validation.md`。

## 阶段 4：MPP 硬件编码（已完成）

目标：将摄像头输出送入 RK MPP，得到稳定的 H.264/H.265 硬件编码码流。

任务：

- 跑通 NV12 到 MPP encoder 的最小编码程序。
- 配置分辨率、帧率、码率、GOP、编码格式和关键帧请求。
- 从内存拷贝版本逐步切换到 DMA-BUF/DRM buffer 低拷贝路径。
- 检查码流时间戳、帧顺序和编码错误恢复。

完成标准：持续生成可正常解码的 1080p30 H.264/H.265 码流，并明确数据拷贝次数和缓冲区所有权。

阶段 4 验收记录（2026-08-25）：

- [x] 固定官方 MPP 1.1.0 并完成可复现 aarch64 SDK/bundle。
- [x] 官方 `mpi_enc_test` 与实际 RKISP NV12 H.264 编码通过。
- [x] 自定义 H.264/H.265 文件编码、CBR/VBR、GOP、请求 IDR 通过。
- [x] H.264/H.265 均由官方 MPP decoder 和独立 FFmpeg 完整解码。
- [x] V4L2 -> MPP H.264 copy path 300 帧、约30fps、0 timeout/drop。
- [x] V4L2 EXPBUF -> MPP EXT_DMA 300 帧通过；color-bar 下 copy/dmabuf 码流
  SHA 完全相同。
- [x] DMA-BUF 移除约1.82ms copy，进程 CPU 8%降至3%；PM 和
  CIF/ISP/MPP/RKVENC/IOMMU fault 检查通过。

Raw Annex-B 输入侧 FPS 可能被 FFmpeg 猜测为25；MPP 配置与 PTS 使用30fps，
阶段5必须由 RTP/容器 timestamps 明确时间基。

## 阶段 5：低延迟视频流（当前阶段）

目标：通过 RTP/RTSP 将编码码流推送到 PC，并控制缓冲和编码延迟。

任务：

- 先用 GStreamer 验证网络发送和 PC 解码，再决定是否编写 C/C++ 推流程序。
- 调整 GOP、B 帧、码率控制、队列长度、播放器缓存和丢帧策略。
- 正确处理 PTS/DTS、断开重连和关键帧恢复。
- 对采集、ISP、编码、网络和播放各段打时间戳。

当前执行结果：

- [x] GStreamer H.264 文件 RTP/UDP 到 Windows 解码基线。
- [x] typed MPP packet sink 与共享 V4L2 DMA-BUF capture。
- [x] 1080p30实时 RTP：1800帧、30.05fps、0 timeout/drop/queue overrun。
- [x] 同屏端到端延迟：100/70/100ms，平均约90ms。
- [x] 抓包验证：3205个RTP包、0 sequence gap，120个timestamp组全部有marker，
  90kHz帧间增量平均2999.99。
- [x] jitter 100/50/30/10ms矩阵；推荐30ms。
- [x] GOP60/30和queue2/1矩阵；推荐GOP30、queue2。
- [x] queue overrun IDR冷却控制器及正常路径板端回归。
- [ ] RTSP shared server、客户端断开重连和关键帧恢复。

完成标准：PC 端稳定播放，端到端延迟可测量，链路断开后能够恢复。

## 阶段 6：性能测量与稳定性

目标：让项目从“能够运行”提升到“行为可解释、性能可复现”。

任务：

- 记录端到端延迟、FPS、丢帧、码率、CPU、内存、DDR 带宽和温度。
- 对比拷贝路径、DMA-BUF 路径以及启用/绕过 RGA 的差异。
- 执行持续推流、反复启停、网络抖动和异常断开测试。
- 整理常见故障的分层定位方法。

完成标准：形成稳定性测试结果和性能对比数据，并能指出当前主要延迟与带宽瓶颈。

## 阶段 7：AI 感知与业务扩展（可选）

目标：在稳定视频链路上接入 RKNPU 推理及后续业务控制。

任务：

- 从视频帧分支出推理输入，避免阻塞主编码链路。
- 使用 RGA 完成模型前处理，RKNPU 完成检测或跟踪。
- 将结果用于 OSD、目标跟踪、云台或飞控消息输出。

完成标准：AI 分支关闭或异常时不影响主视频流，开启后具备明确的推理帧率和新增延迟数据。

## 当前决策

- 后续所有阶段按学习陪练模式推进，不默认由 Codex 直接完成实现。
- 阶段 0-4 已完成，当前从阶段 5“低延迟视频流”开始。
- 正式 `ov13850.c` 作为主学习和交付驱动，`ov13850_i2c_min.c` 仅用于寄存器验证与故障定位。
- 第一版固定为 `1080p30 + H.264 + RTP/RTSP`，链路稳定后再提高分辨率或接入 AI。
- RGA 是按需节点，不需要缩放、旋转或格式转换时直接绕过。
- 阶段 3 对比结论：bypass 资源最低；Direct 消除显式 copy，但是否降低总 CPU
  需结合多轮实测，不能仅凭“零拷贝”名称推断。

## 已知环境限制

| 问题 | 处理 |
| --- | --- |
| 曾有 Codex PowerShell 访问 WSL UNC 路径返回 `Access is denied` | 用户确认可通过 `\\wsl.localhost\Ubuntu-22.04\` 访问，或在 PowerShell 使用 `wsl -d Ubuntu-22.04`（单条命令：`wsl -d Ubuntu-22.04 -- sh -lc '...'`）；需要直接修改或实机测试时优先使用这两种入口 |
| 当前离线 VHDX 读取少数文件可能得到全零 | 深入修改前在 WSL 内使用 `rg`、`cat`、`git` 重新核对目标文件 |

## 工具错误记录

| 时间 | 问题 | 处理 |
| --- | --- | --- |
| 2026-07-16 | 创建交接文档自动更新任务时，`status` 使用小写 `active` 被参数校验拒绝 | 按工具枚举要求改用 `ACTIVE` 后重新创建 |
| 2026-07-16 | 自动化首次保存后被规范化为每 2 小时执行，与用户要求不符 | 显式更新调度规则为每 1 小时，并读取本地 `automation.toml` 确认 |
| 2026-07-16 | 每日多时点调度表达被应用退回成全天每小时执行 | 改用每两小时并限定小时集合的等价表达，成功保存为 00:00、06:00 至 22:00 的偶数整点 |
