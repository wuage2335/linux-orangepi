# RK3588 / OV13850 项目记忆

<!-- camera-status-navigation -->
> 文档同步：2026-09-16。历史数据按原测试条件保留；当前阶段、环境、结果与待办统一见状态入口。
> 最新入口：[Camera 当前状态](CURRENT_STATUS.md)。
<!-- /camera-status-navigation -->

## 2026-09-16 重启后更新

用户已部署DFI时钟overlay并重启，dmc节点和负载采样恢复。完整功能矩阵通过，
3A 180秒4461帧24.76fps、0采集timeout/drop；Windows D3D11解码90帧通过，
退出PM suspended/0、DMC从工作2.4GHz回到空闲534MHz。
DMC load是DFI最忙通道利用率，不是绝对DDR MB/s。精确光学时延与绝对带宽仍未测。
详见[工程收口验证](engineering_closeout_validation.md)顶部，早期“候选未部署”已过时。

## 2026-09-15 实机更新

本轮用户授权自主工程收口，实际工作仓库为WSL `/home/wuage2335/linux-orangepi`，
当前板卡地址`192.168.1.16`。WSL已恢复GStreamer/RTSP开发包；host SDK与aarch64
部署SDK分开管理，完整应用编译和板端功能矩阵通过。
新的一键入口、原子动态库安装修复、3A/固定曝光对照及DDR阻塞证据见
[工程收口验证](engineering_closeout_validation.md)。以下各节保留9月14日的历史快照。

更新日期：2026-09-14（Asia/Hong_Kong）。仓库位置：`linux-orangepi-dev:/workspace/linux-orangepi`。

本文件用于新对话快速恢复项目上下文；正式交接入口仍为 [HANDOFF.md](HANDOFF.md)。本轮依据容器中的文档、Git 状态及少量源码核对整理，未连接开发板、构建或重新执行性能测试。下文实机数据均为项目已有验收记录，不代表今天复测。相对链接以容器内 `docs/codex/PROJECT_MEMORY.md` 为基准；宿主机同名文件仅为阅读副本。

## 1. 项目与当前阶段

- 硬件：Orange Pi 5 Pro / RK3588S，OV13850 CAM2，2-lane MIPI CSI-2。
- 仓库是 Linux 6.1.99 BSP；学习应用集中在 `ov13850_opi5pro_learning/`。
- 目标：构建可复现、可测量、可演示的 1080p 摄像头低延迟视频链路。
- 当前文档确认阶段 0–6 已完成；阶段 7 RKNPU / AI 感知与业务扩展可选，尚未完成。
- 旧跨对话记忆中的“阶段 2 仅有脚手架、等待统一验证”已经过时，不能再作为当前任务。

```text
OV13850 RAW10 -> D-PHY / CSI-2 / CIF -> RKISP -> V4L2 NV12
-> DMA-BUF -> MPP H.264 -> GStreamer RTP/RTSP -> Windows D3D11 显示

RKISP stats -> RKAIQ AE/AWB -> Sensor controls + ISP params -> 后续帧
```

主链路无需缩放时绕过 RGA。RGA 已验证文件及实时 copy/direct-MMAP 缩放实验，尚未集成 `RGA destination DMA-BUF -> MPP`。MPP 核心支持 H.264/H.265 文件编码，当前 RTSP 下游仅实现 H.264。

## 2. 协作与变更约定

- 用户是主要实践者；Codex 是导师、结对伙伴和评审者。
- 默认：讲原理、拆小任务、用户动手、检查结果、定位问题、总结；完整阶段代码代写需要用户明确授权。
- 本轮授权范围是阅读、熟悉和建立记忆，不表示授权开始 AI 阶段或部署内核。
- 变更前核对 Git 状态，保留既有工作；不得默默覆盖、丢弃或强推。
- 完成结论需要对应构建、解码或实机证据；文档写完不能替代功能验收。
- 实际板端 IP、设备节点、DT、内核、PM 和服务状态必须重新查询。
- 内核使用独立 `O=`；相关 Image 构建保留项目约定 `KCFLAGS="-Wa,-I,$PWD"`。遇真实 Werror 警告修源码后重测。
- 私有 MPP / RKAIQ bundle 不覆盖系统库；RKAIQ IQ 转换在私有目录中进行，不覆盖 `/etc/iqfiles`。

## 3. 本轮仓库快照

2026-09-14 已现场确认：

- 容器 ID `e757a732a4c9`，名称 `linux-orangepi-dev`，运行中。
- 实际工作树 `/workspace/linux-orangepi`；不要把 macOS 上历史同名副本当作活动源码树。
- 分支 `main`，HEAD `5cbbd465d`，提交标题 `docs(streaming): explain parsing and worker shutdown`。
- `git status -sb` 显示 `main...origin/main`，无 ahead/behind 标记；本轮未 fetch，因此只代表本地远端跟踪引用。
- 原有未提交修改：`ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp` 增加一行 `WorkerResult` 中文注释。必须保留。
- 没有切分支、提交、推送、启动摄像头或替换板端文件。

## 4. 文档阅读入口

| 目的 | 入口 |
| --- | --- |
| 快速恢复 | [README](README.md)、[HANDOFF](HANDOFF.md)、[task_plan](task_plan.md)、[progress](progress.md) |
| 架构与源码路线 | [源码导读](../project-deep-dive/README.md)、[架构](../project-deep-dive/03_architecture.md)、[运行流程](../project-deep-dive/04_runtime_flow.md)、[源码地图](../project-deep-dive/07_code_map.md) |
| 运行与排障 | [快速运行](../project-deep-dive/02_quick_start.md)、[分层排障](../project-deep-dive/06_debug_guide.md)、[内核故障记录](orangepi5pro-kernel-troubleshooting.md) |
| 性能与拷贝边界 | [量化结果](camera_pipeline_quantitative_results.md)、[分阶段耗时](pipeline_stage_timing_validation.md)、[数据流与拷贝](camera_data_flow_and_copy_analysis.md) |
| 网络与 3A 验收 | [RTP](stage5_rtp_streaming_validation.md)、[RTSP 与重连](stage5_rtsp_recovery_validation.md)、[RKAIQ/3A](stage6_rkaiq_3a_validation.md) |
| 面试表达 | [STAR 与改进](../interview/interview_project_star_and_improvements.md) |
| 子项目使用 | [streaming](../../ov13850_opi5pro_learning/streaming/README.md)、[RKAIQ](../../ov13850_opi5pro_learning/rkaiq/README.md) |

本轮精读入口、交接、阶段计划、最新进展、架构、运行流程、源码地图、数据拷贝、计时、快速运行与分层调试；另外检查量化、3A、排障、技术与面试资料的相关段落或索引。没有逐行读完整 Linux `Documentation/`、全部历史计划、面试逐字稿和图片附件。

## 5. 代码地图与必须记住的契约

下列路径相对仓库根目录：

| 文件 | 职责与重点 |
| --- | --- |
| `drivers/media/i2c/ov13850_i2c_min.c` | 实际完成验证的学习 sensor subdev：模式、controls、PM、stream、module-info |
| `drivers/media/i2c/ov13850.c` | 正式/参考驱动，用于对照；不得与学习 binding 竞争 |
| `arch/arm64/boot/dts/rockchip/rk3588s-orangepi-5-pro-camera2.dtsi` | CAM2 板级连接，配合 `overlay/rk3588-opi5pro-cam2*.dts` |
| `ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh` | 运行前配置媒体图、crop 和 mainpath；C++ 只验证输入合同 |
| `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp` | 最终业务入口：main 的 GLib 循环与 capture worker |
| `ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp` | REQBUFS/QUERYBUF/MMAP/EXPBUF，DQBUF/QBUF 所有权 |
| `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp` | MPP 初始化、copy/external buffer 编码、header、IDR、释放 |
| `ov13850_opi5pro_learning/mpp/src/encoded_packet_sink.hpp` | 非拥有 packet view 和输出接口，解耦编码与网络 |
| `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp` | shared media、客户端、SPS/PPS、IDR、有界队列与同步 |
| `ov13850_opi5pro_learning/streaming/src/live_pts_clock.hpp` | 单调实时时钟 PTS，避免固定帧率时间基漂移 |
| `ov13850_opi5pro_learning/rkaiq/` | 私有 bundle、module-info shim、IQ 转换、兼容 patch 与场景验证 |
| `ov13850_opi5pro_learning/benchmarks/src/pipeline_stage_benchmark.cpp` | 逐帧打点、CSV、分位数与测量口径 |

关键约束：

1. 学习 binding 只能是 `learning,ov13850-i2c`；该板历史成功基线采用 `CONFIG_VIDEO_OV13850_I2C_MIN=y`。晚加载模块会错过 CIF/ISP async notifier 组图窗口。
2. `v4l2_i2c_subdev_init()` 后 clientdata 是 `v4l2_subdev *`，通过 `ov13850_min_from_client()` / `to_ov13850_min()` 转回私有对象。
3. 开流：runtime PM -> global registers -> mode -> controls replay -> `0x0100=1`；停流后释放 PM 引用。
4. NV12 有效数据为 1920x1080、3,110,400 bytes；DMA-BUF 外部布局 `ver_stride=1080`，copy 内部 padding 为 1088。分配大小不能代替 UV offset。
5. copy 可以在复制后归还 V4L2 buffer；DMA-BUF 必须等待同步编码使用完 buffer 后 QBUF，防止 ISP 覆盖。
6. main 必须 join worker 后再销毁其借用的 sink；统计和异常在 join 后读取。
7. 新 RTSP 客户端需要 SPS/PPS + IDR；无客户端时不积压历史视频。
8. DMA-BUF 消除的是 V4L2 到 MPP 的 CPU 整帧复制；压缩 packet 到 GstBuffer 仍有 copy，硬件 DDR 读写仍存在，不能称全链路零拷贝。

## 6. 已有验证结果与测量边界

| 项目 | 历史证据结论 |
| --- | --- |
| 驱动双模式 | 2112x1568 RAW10 约 29.97fps；4224x3136 约 7.51fps；重复启停及 PM 通过 |
| V4L2 合规 | 42/43，control event subscription 为非阻塞遗留，不能说全部通过 |
| RTP | 1800 帧、30.05fps、0 timeout/drop/queue overrun |
| RTSP 长会话 | 21,561 帧、717.58 秒、30.05fps、0 timeout/drop；不是 24 小时 soak |
| Stage 5 同屏延迟 | 五组 60/70/10/160/60ms，均值 72ms；少量历史样本，不是 3A 实景延迟保证或 P95 |
| 固定 30fps 计时（9 月 2 日） | SOF 到 DQBUF P50 28.501ms；copy P50 2.030ms；MPP DMA-BUF P50 4.832ms |
| 真实约 8Mbps Gst push | P50 60.67us，只到 appsrc push 返回，不含网络、解码或显示 |
| copy / DMA-BUF 资源 | 同轮 CPU 8.2% / 3.2%；post-DQ 均值 6.872 / 5.230ms |
| 3A | AE/AWB 与亮/普通/暗三场景通过；普通用户 stats 线程失败回退 `SCHED_OTHER` 后闭环成立 |

最新条件修正：9 月 2 日暗场 AE 将 exposure/VBLANK 调至 2995/1449，帧周期约 60.339ms（16.57fps）。这是曝光/VTS 策略的变化，不是 3A CPU 算法耗时 60ms。早期彩条下 3A 开关均约 30fps 的结论不能推广到所有照度。

同轮计时文档已有温度 44.384→47.153°C；因此“从未测温度”不再准确。但它不能替代受控 3A 开关温度对比或 24 小时热稳测试。DDR 计数器带宽、3A 正常实景精确同屏延迟、独立 Wi-Fi 单程耗时仍缺证据。帧周期、SOF/ISP 延迟及编码流水有重叠，禁止机械相加。

## 7. 运行与故障恢复记忆

- 推荐已验证参数：1080p、H.264 CBR target 8Mbps、GOP30、无 B 帧、MTU1200、queue2、DMA-BUF、接收 jitter30ms；RTSP `8554/live`。
- 历史内核 `6.1.99-opi5pro-livecfg-baseline`，历史 mainpath `/dev/video11`、stats `/dev/video18`，运行前重新确认。
- 带 3A 演示入口见 [README 快速视频传输](README.md)：启动私有 RKAIQ、配置 ISP、启动 RTSP、Windows GStreamer 接收。
- 新内核无 wlan0：检查匹配 kernelrelease 的 `bcmdhd.ko` 与模块树，不能仅凭 SSH 失联判定内核没启动。
- STREAMON ENOMEM 的历史根因是 `enum_frame_interval(code=0)` 被错误拒绝，导致 CIF dummy buffer size 0，并非已证实 CMA 不足。
- MPP 成功但颜色错：先查 1080/1088 stride 与 QBUF 时机。
- RTSP 约一分钟后花屏/积延迟：历史根因是 30.05fps 实采与固定 30fps PTS 的漂移；单调时钟修复后验证通过。
- 偏暗偏绿：检查 RKAIQ、IQ、动态 stats 和 AE/AWB，不要直接归因于 MPP/RTP。
- module-info 候选 Image 已构建但历史记录明确未部署；不能把源码有 ioctl 等同于活动内核有 ioctl。
- OV13855 ID 000000 是独立未接 sensor / DT 清理事项，不与 OV13850 改动混合部署。

## 8. 文档冲突与继续前注意事项

- `HANDOFF.md` 页首更新时间仍写 8 月 30 日，但正文已有 9 月 2 日计时追加；旧段落“当前进入阶段 5”不能覆盖顶部及 task_plan 的阶段 0–6 完成结论。
- `task_plan.md` 尾部“最小驱动仅用于寄存器验证”的描述与阶段 2 完成记录、HANDOFF 和源码导读冲突。现有文档证据支持完整学习驱动已被验收；活动板端绑定仍需实查。
- `RK3588_camera_technical_points.md` 保留 8 月 6 日阶段 2 脚手架叙述，已经过时。
- streaming README 的“3A 待解决”是历史段落；以 Stage 6 验收和 9 月计时文档为准。
- quick_start 的 `.ko` 编译示例不表示晚加载可组成历史成功媒体图；遵守内建驱动验收边界。
- Codex README 的面试文档链接仍指同目录，实际文件在 `docs/interview/`；本记忆已使用正确路径。
- 本轮只新增记忆及入口，不批量改写历史验收记录。以后有新实测，更新 HANDOFF、progress、task_plan 及本文件，保留日期、测试条件和证据路径。

下一次对话先读本文件和 HANDOFF，再核对 Git 状态；用户若继续源码学习，可从 RTSP `main()` 的 worker 停止、join 和异常传递开始，但这只是近期提交提供的阅读线索，不是已确认的新开发任务。
