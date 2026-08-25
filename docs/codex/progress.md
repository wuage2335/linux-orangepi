# linux-orangepi 阅读进度

## 2026-08-25 阶段 5 RTP里程碑

- 板端 GStreamer 1.20.3 与 Windows GStreamer 1.28.6 环境验证完成；Windows
  使用 NVIDIA D3D11 H.264 hardware decode/display。
- MPP 输出由 ostream-only 重构为同步 typed packet sink，并提取共享 V4L2
  MMAP/EXPBUF capture；Stage 4 文件、H.264/H.265 参数矩阵和实时双路径回归通过。
- 实时 `V4L2 DMA-BUF -> MPP H.264 -> appsrc -> RTP/UDP` 完成1800帧，
  30.05fps、0 timeout、0 sequence drop、0 queue overrun，停流后PM suspended/0。
- 同屏计时器三组端到端延迟为100/70/100ms，范围70–100ms，平均约90ms，满足
  第一版不高于200ms目标，60秒运行无持续积帧。
- 画面偏暗偏绿已定位到未运行 RKAIQ 3A/IQ：exposure 1536/1648、analogue
  gain仍为最小16；它不是MPP/RTP/D3D11问题。
- 统一量化结果和测量方法见 `camera_pipeline_quantitative_results.md`。Stage 5
  仍未完成：packet timing细化、RTSP和断线重连留待后续。

## 2026-08-25 阶段 4

- 固定官方 Rockchip MPP 1.1.0、commit `c08762ebf`，建立可复现 aarch64 SDK
  builder 和自包含 bundle；板端不做系统级 MPP 安装。
- `/dev/mpp_service`、RKVENC/VEPU2 和官方 `mpi_enc_test` 验证通过；VENC
  regulator/devfreq 启动告警不阻塞当前硬件编码。
- 自定义 1920x1080 NV12 H.264/H.265 文件编码通过，支持 CBR/VBR、4/8/12
  Mbps、GOP30/60 和请求 IDR；官方 decoder 与 FFmpeg 均完整解码。
- 实时 V4L2->MPP H.264 copy path 完成300帧、30.03fps、0 timeout/drop；按
  3,815,189 bytes和约9.99秒重算，动态场景平均码率约3.06Mbps。
- `/dev/video11` 支持4个 EXPBUF，均为3,133,440 bytes；MPP EXT_DMA 原型完成
  300帧。Color-bar 下 copy/dmabuf码流 SHA 完全相同。
- DMA-BUF 路径必须使用 ver_stride=1080 解释外部 NV12；copy内部 buffer 使用
  1088 padding。DMA 消除约1.82ms copy，进程 CPU 从8%降到3%。
- PM 为 suspended/usage 0，无新增 CIF/ISP/MPP/RKVENC/IOMMU fault。
- 阶段 4 完成，当前进入阶段 5 RTP/RTSP 低延迟视频流。

## 2026-08-25

- 阶段 3 ISP/RGA 功能与对比目标完成：RKISP mainpath 稳定输出 1920x1080
  NV12@30，文件式 RGA、实时 copy 和实时 direct-MMAP 均通过实机验收。
- 实时 copy/direct 都处理 300 帧，30.04 fps、0 timeout、0 sequence drop；退出
  后 sensor PM 为 suspended/usage 0，无新增 CIF/ISP/RGA/MMU/IOMMU fault。
- Copy 路径为 `DQBUF -> memcpy -> QBUF -> RGA`；Direct 路径在 STREAMON 前
  导入 4 个 MMAP 地址，运行时为 `DQBUF -> RGA -> QBUF`。
- 联合测试中 copy/direct 进程 CPU 为 7.11%/2.64%；独立 benchmark 为
  4.95%/5.41%。Direct 确定移除了 copy 并降低 RSS，但 total CPU 与 RGA 耗时
  存在波动，不能声称每轮都更快。
- bypass/copy/direct 外部 CPU 分别约 1%/5%/5%，最大 RSS 为
  12,844/19,564/16,380 KB；三条路径均受 sensor 30 fps 节奏限制。
- 当前按需变换只有 resize；旋转/色彩转换无业务需求。DMA-BUF 留到后续低延迟
  优化。下一阶段为 RK MPP H.264/H.265 硬件编码。
- 详细证据：`rga_nv12_file_resize_validation.md`、
  `rga_v4l2_live_validation.md` 和 `rga_v4l2_direct_comparison_validation.md`。

## 2026-08-21

- 完成 `ov13850_i2c_min.c` 阶段 2：controls、runtime PM、两模式 TRY/ACTIVE、
  stream lifecycle、probe/remove 清理和隔离 binding。
- 模块装卸和 alias 验证通过，但实机证明晚加载会错过 Rockchip CIF/ISP async
  notifier；最终使用 `CONFIG_VIDEO_OV13850_I2C_MIN=y`。
- 当前学习 binding 为 `learning,ov13850-i2c`，实机 SSH 为 `192.168.1.10`，
  media graph 已包含 sensor -> D-PHY -> CSI2 -> CIF。
- STREAMON 的 ENOMEM 根因是 `enum_frame_interval()` 拒绝 CIF 的 `code=0`，使
  dummy buffer size 为 0；改为仅在 code 非零时校验后，同一测试转为成功。
- 2112x1568 RAW10 单帧 4,415,488 字节、29.97 fps；4224x3136 RAW10 单帧
  16,859,136 字节、7.51 fps。低分辨率连续 5 次启停和高分辨率持续 60 帧成功。
- TRY/ACTIVE、流中 `-EBUSY`、controls 范围及曝光/增益/VTS/测试图寄存器写入
  均通过；停流后 PM 为 suspended、usage 0，无新增 CRC/ECC/timeout/overflow。
- `v4l2-compliance` 为 42/43；control event 订阅是唯一非阻塞遗留项。
- 当前 Image SHA256 为
  `e5312723b9192fdb59fcf60b6770490e149888f8ec44d002cbde0ee5699d0f19`；旧 Image
  回滚副本为 `/home/orangepi/boot-backups/Image.before-stage2-builtin-20260821_162915`。

## 2026-08-06

- 将阶段 2 的学习驱动设计和实施计划存入仓库：`docs/superpowers/specs/2026-08-03-ov13850-stage2-learning-driver-design.md` 与 `docs/superpowers/plans/2026-08-03-ov13850-stage2-learning-driver-plan.md`。
- 用户选择以 `ov13850_i2c_min.c` 作为阶段 2 的练习实现，正式 `ov13850.c` 仅作只读参考；学习 binding 只能是 `learning,ov13850-i2c`。
- 用户明确要求先完成阶段 2 编写，再统一构建和上机验证。当前没有新增构建、模块装卸、设备树绑定或板端采集证据。
- 已写入 2A 的部分静态脚手架：V4L2 control/runtime-PM 头文件、control 状态字段、寄存器常量、link-frequency/测试图菜单，并移除了竞争性的 `i2c:ovti,ov13850` alias。
- 阶段性提交 `5f9f87714 wip(ov13850): add stage 2 learning scaffolding` 包含上述脚手架和两份设计文档；后续继续前需重新检查其推送状态。

## 2026-07-15

- 已加载 `using-superpowers` 技能。
- 已加载 `planning-with-files-zh` 技能，用文件记录本次多步骤阅读任务。
- 已确认当前 Windows 共享工作区只有 `.agents` 和 `.git`，阅读记录将放在这里，暂不改动 WSL 项目。
- 尝试 `wsl.exe -l -v`、`wsl.exe --list --all --verbose` 和 `wsl.exe -e sh ...`，当前 Codex 进程看到的是无可用 WSL 发行版状态。
- 用户更正/确认 Windows 访问路径应为 `\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi`；当前 Codex PowerShell 对该 UNC 路径执行 `Test-Path` 返回 `Access is denied`，所以此前使用 VHDX 离线读取是权限受限下的临时方案。
- 已确认 `C:\Users\Administrator\AppData\Local\Packages\CanonicalGroupLimited.Ubuntu22.04LTS_79rhkp1fndgsc\LocalState\ext4.vhdx` 存在，说明磁盘上有 Ubuntu WSL 数据文件，但当前未注册/未挂载。
- 尝试只读 `wsl --mount ... --options ro` 失败，错误为 `ERROR_SHARING_VIOLATION`，说明 VHDX 被 WSL 进程占用。
- 已创建 `tools/read_vhdx_ext4.py`，通过只读解析 VHDX 和 ext4 成功读取根目录与 `/home/wuage2335/linux-orangepi`。
- 已阅读顶层 `README`、`Makefile`、`boot.its`、`docs/orangepi5-ultra-kernel-reading-guide.md`、`docs/orangepi5-ultra-kernel-feature-map.md`。
- 已阅读主设备树 `rk3588-orangepi-5-ultra.dts`、DTB/DTBO Makefile、关键 defconfig 命中项、RKNPU Kconfig/Makefile、OV13850 sensor 驱动片段。
- 发现离线读取脚本对部分文件返回全零，已记录为当前方法限制。
- 已确认 WSL `main` 包含 `ov13850_i2c_min.c` 最小学习驱动；Windows partial 副本包含 `ov13850_new.c` 等不同实验分支内容。

## 2026-07-16

- 根据用户反馈，将当前任务从“项目通读”切换为“RK3588 摄像头低延迟链路实施”。
- 删除阶段中的天/周时间表示，统一改为阶段编号。
- 阶段 0“基线验证”和阶段 1“传感器与 DTS”已标记完成。
- 阶段 2“V4L2 驱动完善”设为当前阶段，后续阶段为 ISP/RGA、MPP、低延迟视频流、性能稳定性和可选 AI 扩展。
- 已将完整计划整理为独立文档 `docs/RK3588摄像头低延迟链路项目计划.md`，包含项目目标、阶段状态、阶段任务、验收标准、当前执行入口和环境说明。
- 已创建 `docs/对话任务交接文档.md`，用于新对话快速恢复目标、阶段状态、关键事实、环境限制和下一步操作。
- 首次创建每小时自动更新任务时，因状态值应为大写 `ACTIVE` 而被参数校验拒绝；已记录并准备使用正确枚举重试。
- 已成功创建每小时自动化“更新 RK3588 任务交接文档”，自动化标识为 `rk3588`，限定在当前 `Camera开发` 项目中运行并默认只修改交接文档。
- 核验时发现自动化首次落盘为每 2 小时，已显式修正为每 1 小时；本地配置现为 `ACTIVE` 且项目目标为当前 `Camera开发` 工作区。
- 用户将交接文档自动更新调整为每天 06:00 至 24:00 每两小时一次；已核验并同步为 00:00、06:00、08:00、10:00、12:00、14:00、16:00、18:00、20:00、22:00 触发，其中 00:00 对应前一日 24:00。
- 用户明确本项目是学习项目：用户是主要实践者，Codex 应以导师和结对伙伴方式推进；未经明确授权，不得一次性代写完整阶段、完整功能或大批量代码。该原则已写入任务计划、完整项目计划和交接文档。
- 当时已只读审查 WSL `main` 中的 `drivers/media/i2c/ov13850_i2c_min.c` 并对照正式 `ov13850.c`；随后源码已补上 `max_fps`、正确的 mbus-config 回调和最小 `.s_stream`。Controls、runtime PM 与完整格式协商仍待完成；最新状态以 2026-08-06 记录为准。
- 进一步确认 `v4l2_i2c_subdev_init()` 会改变 I2C clientdata 的对象类型；当前 sysfs 回调和 `remove()` 的直接私有结构体转换不成立，已将其纳入 2A 的基础装卸修正范围。

## 2026-07-18

- 从 Orange Pi 5 Pro 正常运行系统导出 `/proc/config.gz`，建立独立输出目录 `out/orangepi5pro-livecfg-baseline`，构建并验证 `6.1.99-opi5pro-livecfg-baseline`。
- 通过只替换 Image 的单变量测试确认 6.1.99 内核能够在 5 Pro 上启动；第一次停在 `Starting kernel ...` 的问题范围缩小到旧配置、替换后的 DTB/overlay 或组合不一致，尚未定位到单一根因。
- 确认新内核启动后 SSH 消失的直接原因是缺少对应 `/lib/modules/<release>`，导致板载 Broadcom `bcmdhd` 无法加载、`wlan0` 不存在。
- 定向构建并安装匹配新 release 的 `bcmdhd.ko`，执行 `depmod` 后 Wi-Fi、默认路由和 SSH 恢复。
- 在 `ov13850_opi5pro_learning` 下创建 `orangepi5pro-kernel-troubleshooting.md`，记录启动、模块、独立 `O=` 构建、回滚和摄像头绑定问题。

## 2026-07-20

- 开始将 Windows `Camera开发` 工作区中的 Camera 项目文档同步到 `linux-orangepi/docs/codex/`，作为其他 Codex 对话的统一接手入口。
- 更新 handoff，明确最近一次实机验证时间、当前 SSH 超时状态、仓库文档路径和下一步单变量验证顺序。
