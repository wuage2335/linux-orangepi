# RK3588 摄像头链路任务交接文档

> 用途：在新建 Codex 对话时快速恢复任务上下文。最后更新：2026-08-28（Asia/Shanghai）。

## 0. 协作约束

这是学习项目。用户是主要实践者；Codex 负责原理讲解、拆分步骤、审查结果和
诊断。代码或板端修改前先说明目标、文件和风险；未经用户明确授权，不得一次性
代写完整阶段。阶段 4 已完成官方 MPP、文件编码、实时 copy 和 DMA-BUF 验证；
阶段 5 的 RTP/UDP、packet timing、RTSP和重连已通过，下一主线为阶段6。任何
完成结论都必须有构建、码流解码或实机日志证据。

## 1. 当前状态

- 项目：Orange Pi 5 Pro（RK3588S）上的 OV13850 CAM2 摄像头链路。
- 项目阶段：阶段 0-5 已完成实机验收；下一主线为阶段 6“性能测量与稳定性”。
  RTP/UDP已完成1800帧、30.05fps、0 timeout/drop/overrun；shared RTSP、
  GStreamer/VLC重连和关键帧恢复均通过。
- 正式交付/参考驱动：`drivers/media/i2c/ov13850.c`。
- 当前学习实现：`drivers/media/i2c/ov13850_i2c_min.c`，仅能绑定
  `learning,ov13850-i2c`，不得与正式 `ovti,ov13850` binding 竞争。
- 阶段 2A-2F 已实现并完成统一构建/实机验证：controls、runtime PM、两模式
  TRY/ACTIVE、完整 lifecycle、内建启动、media graph、双模式采集和重复启停。
- 当前内核 release：`6.1.99-opi5pro-livecfg-baseline`；学习驱动必须为
  `CONFIG_VIDEO_OV13850_I2C_MIN=y`。作为模块晚加载时会错过 Rockchip CIF/ISP
  async notifier 的组图窗口，虽然 I2C probe 成功，但不会生成 sensor subdev。
- 阶段 3 已验证 `2112x1568 RAW10 -> RKISP -> 1920x1080 NV12@30`，并完成
  `1920x1080 NV12 -> RGA imresize -> 1280x720 NV12` 文件实验。RGA 使用官方
  librga 1.10.6_[3]，板端 multicore driver v1.3.7；详细证据见
  `docs/codex/rga_nv12_file_resize_validation.md`。
- 阶段 3 第一版实时 copy path 也已通过：`/dev/video11 -> V4L2 MMAP ->
  memcpy -> QBUF -> RGA` 连续处理 300 帧，30.04 fps、0 timeout、0 drop；
  详细证据见 `docs/codex/rga_v4l2_live_validation.md`。
- 阶段 5 已新增 typed MPP packet sink、共享 V4L2 capture、GStreamer appsrc RTP
  sink 和实时 DMA-BUF sender。详细量化数据和方法见
  `docs/codex/camera_pipeline_quantitative_results.md`。RTSP详细证据见
  `docs/codex/stage5_rtsp_recovery_validation.md`。
- Task 8抓包为3205个RTP包、0 sequence gap，120个timestamp组全部有marker，
  90kHz帧间增量平均2999.99。jitter/GOP/queue推荐为30ms/30/2。
- Task 9 shared RTSP使用单摄像头、单MPP encoder和shared media factory。旧版
  固定30fps PTS在30.05fps实采下每分钟超前约95ms，40-60秒后花屏、卡顿、
  延迟累积；改用单调实时时钟后，GStreamer连续两分钟稳定且可重连。
- 修复后五组RTSP延迟为60/70/10/160/60ms，平均72ms、中位60ms、最大160ms，
  无单调漂移。VLC播放和重连稳定，但默认约400ms，仅作兼容性验证。
- 2026-08-28总回归：RTP 300帧30.03fps、0 timeout/drop/overrun；RTSP两次
  客户端各解码176帧；阶段4 H.264/H.265、copy/DMA-BUF和官方decoder均通过；
  PM suspended/0且无新增CSI/ISP/MPP/IOMMU fault。
- 当前板端 `/boot/Image` SHA256：
  `e5312723b9192fdb59fcf60b6770490e149888f8ec44d002cbde0ee5699d0f19`。
- 当前分支：`main`；阶段 2 学习驱动源码提交为
  `592d4171c feat(ov13850): complete stage 2 learning driver`。验证文档提交见最新
  `git log`；继续前始终重新读取 `git status -sb`。

## 2. 已验证的历史事实与当前边界

### 2.1 启动与网络基线

- 已验证内核 release：`6.1.99-opi5pro-livecfg-baseline`。
- 历史问题“串口停在 `Starting kernel ...`”的安全结论：只替换 Image 的基线可以
  启动；先前失败是多变量部署（Image、DTB、overlay、uInitrd、模块、启动配置）
  的组合问题，尚未定位到唯一 Kconfig 或 DT 根因。
- 新 release 缺少 Broadcom `bcmdhd.ko` 会导致 `wlan0` 不存在、SSH 失联；最小
  匹配模块树已是历史上验证过的恢复方式。

### 2.2 网络地址必须区分

- 历史 Wi-Fi 网络：`orangepi@192.168.1.17`，网关 `192.168.1.1`；SSID 未记录。
- 后来观测到的 Wi-Fi 网络：SSID `1702`，DHCP 地址
  `orangepi@192.168.0.112`。
- 两者都是历史记录，不代表板卡当前在线。任何 SSH、部署或摄像头结论前，先从
  串口或实际网络重新确认地址、`uname -r`、`wlan0` 和默认路由。
- 2026-08-21 实机验证使用的当前地址：`orangepi@192.168.1.10`。它仍可能随
  DHCP 变化，不能写死到部署脚本。
- 串口参数：`1500000 8N1`；历史 Windows 端口为 `COM20`。

### 2.3 OV13850 历史修复证据

正式驱动曾在 CAM2 冷启动时第一次读到 sensor ID `000000`。已验证的修复是在
`ov13850_probe()` 中，将这一次的 `-ENODEV` 清理资源后转换为 `-EPROBE_DEFER`。
随后重试探测到 `OV00d850`，media graph 闭合，且 `BG10 2112x1568` 单帧 RAW10
采集得到 4,415,488 字节文件。该证据说明当时的正式驱动、D-PHY、CSI/CIF 与 ISP
链路可工作；它不替代当前学习驱动的验证。

未连接的 `ov13855-2@36` 会报告 ID `000000`，是独立 DT 清理项，禁止与 OV13850
学习驱动改动混合部署。

## 3. 环境与路径

- Docker 实际工作树：`linux-orangepi-dev:/workspace/linux-orangepi`。
- WSL 发行版：`Ubuntu-22.04`；源码历史路径：`/home/wuage2335/linux-orangepi`。
- 交叉编译前缀：`aarch64-linux-gnu-`。
- 基线输出目录：`out/orangepi5pro-livecfg-baseline`。
- 学习模块输出目录：`out/orangepi5pro-2a-learning`。
- 所有内核构建必须使用独立 `O=`；独立输出构建 Image 时保留
  `KCFLAGS="-Wa,-I,$PWD"`。

## 4. 阶段 2 的设计与实施入口

按此顺序阅读：

1. `docs/codex/README.md`
2. `docs/superpowers/specs/2026-08-03-ov13850-stage2-learning-driver-design.md`
3. `docs/superpowers/plans/2026-08-03-ov13850-stage2-learning-driver-plan.md`
4. `drivers/media/i2c/ov13850_i2c_min.c`
5. `drivers/media/i2c/ov13850.c`（只读对照）

学习驱动目标是标准 V4L2 sensor sub-device：两个 RAW10 BGGR 模式
（2112x1568@30 与 4224x3136@7.5）、LINK_FREQ/PIXEL_RATE/HBLANK、VBLANK、
曝光、模拟增益与测试图 controls，以及由 `s_stream()` 持有引用的 runtime PM。

`v4l2_i2c_subdev_init()` 之后，I2C clientdata 是 `struct v4l2_subdev *`；必须
通过 `ov13850_min_from_client()` 和 `to_ov13850_min()` 回到私有结构，不能直接
转换成 `struct ov13850_min *`。

## 5. 阶段 2 实机验收结果（2026-08-21）

1. 模块 vermagic/alias 和装卸通过，但晚加载无法进入 media graph；改为内建后，
   sensor 在启动约 6.7 秒 probe，并形成 `sensor -> D-PHY -> CSI2 -> CIF` 链路。
2. 首次 STREAMON 的 ENOMEM 不是 CMA 不足，而是 `enum_frame_interval()` 拒绝
   CIF 传入的 `code=0`，使 dummy buffer size 为 0。仅在非零 code 时校验后，
   同一测试由失败转为成功。
3. 2112x1568 RAW10 单帧 4,415,488 字节、持续约 29.97 fps；4224x3136 RAW10
   单帧 16,859,136 字节、持续约 7.51 fps。
4. TRY 不改变 ACTIVE；流中切 ACTIVE 返回 `-EBUSY`；controls 寄存器写入正确。
   低分辨率连续 5 次启停成功，停流后 PM 均为 `suspended`、usage 0，无新增
   CRC/ECC/timeout/overflow。
5. `v4l2-compliance` 42/43；唯一失败为 control event 订阅。正式参考驱动也未实现
   该接口，当前记为非阻塞标准化遗留项。

## 6. 阶段 3 ISP/RGA 验收结果（2026-08-24）

1. RKISP mainpath 已稳定输出 1920x1080 NV12，单帧 3,110,400 字节，持续约
   30.05 fps；中心裁剪 2112x1188 后按 10/11 等比例缩放，无几何变形。
2. 项目内固定官方 librga API 1.10.6_[3]，提交
   `2b32edcb97b601b25683e2941d888c8515da6d55`，aarch64 动态库通过
   `$ORIGIN/../lib` 从部署包加载。板端 RGA multicore driver 为 v1.3.7。
3. 文件式 `1920x1080 NV12 -> 1280x720 NV12` 黑盒测试通过，输出
   1,382,400 字节，SHA-256 为
   `2f1d2bc1fbbe822ade7536d39f198b9affe4bf45fbd898b59735f0041a5d0deb`。
4. 100 次同步缩放的观测平均耗时为 1.63-2.45 ms，纯缩放吞吐约 408-614
   次/秒；该数据不包含文件 I/O、V4L2、sensor 或 ISP，不能当作端到端延迟。
5. 输出 Y/UV 平面尺寸和数值范围有效，重复输出一致；本次运行无新增
   RGA/MMU/IOMMU fault。
6. 实时 copy path 预丢弃 3 帧后处理 300 帧，显式 memcpy 平均 0.731 ms，
   同步 RGA 平均 2.593 ms，完整循环 30.04 fps；V4L2 sequence drop 和 poll
   timeout 都为 0。
7. 实时输出 1280x720 NV12 为 1,382,400 字节，SHA-256 为
   `5b11f6f24b6469f361821397ba986f43bb450d8a2965a2f9d91fcd4b4e26f68b`；
   退出后 sensor PM 为 suspended/usage 0，新增日志无 CIF/ISP/RGA/MMU/IOMMU
   fault、timeout 或 overflow。

`ov13855-2@36` 仍是独立 DT 清理项，不得混入后续工作。

### 6.1 阶段 3 Direct-MMAP 对比收口（2026-08-25）

1. `rga_v4l2_live` 保持旧 copy CLI，并新增 `--direct`；Direct 在 STREAMON 前
   导入 4 个 MMAP 地址，帧顺序为 `DQBUF -> RGA -> QBUF`。
2. Copy/direct 联合测试均处理 300 帧，30.04 fps、0 timeout、0 drop。该轮
   copy CPU 7.11%，direct 2.64%。
3. 独立 GNU time benchmark 中 bypass/copy/direct 外部 CPU 分别为 1%/5%/5%，
   RSS 为 12,844/19,564/16,380 KB。Direct 移除了 copy 并降低 RSS，但该轮
   system CPU 和 RGA 时间高于 copy，因此不声称总 CPU 或时延必然更低。
4. Copy/direct 输出大小和 Y/UV 范围均有效；PM 为 suspended/usage 0，无新增
   CIF/ISP/RGA/MMU/IOMMU fault、timeout 或 overflow。
5. 阶段 3 的按需变换只有 resize；旋转/色彩转换无当前业务需求。DMA-BUF 留到
   低延迟优化。

阶段 3 功能与对比目标完成。

## 7. 阶段 4 MPP 验收结果（2026-08-25）

1. 官方 MPP 1.1.0、commit `c08762ebf` 完成 aarch64 交叉构建并以 bundle
   部署；`/dev/mpp_service` 和 RKVENC HW ID `0x50603312` 可用。
2. 官方样例和项目自有 H.264/H.265 文件编码均通过；CBR/VBR、GOP30/60、运行
   中请求 IDR 有实机证据。
3. 自定义 H.264 静态输入300帧由官方 decoder 和 FFmpeg完整解码；H.264/H.265
   参数码流各30帧也完成双重解码。
4. V4L2 H.264 copy path：300帧、30.03fps、0 timeout/drop；按3,815,189 bytes
   和约9.99秒重算，动态场景平均码率约3.06Mbps。
5. DMA-BUF path：`EXPBUF -> MPP_BUFFER_TYPE_EXT_DMA` 300帧通过。Color-bar 下
   copy/dmabuf码流 SHA 完全相同；DMA 消除约1.82ms copy，CPU 8%降至3%。
6. V4L2 有效 NV12 UV offset 对应 ver_stride=1080；copy MppBuffer 使用1088
   padding。曾用1088解释外部 buffer 会得到错误色度/异常小码流，已修复。
7. 退出后 sensor PM 为 suspended/usage 0，无新增 CIF/ISP/MPP/RKVENC/IOMMU
   fault。启动时 VENC regulator/devfreq 告警不阻塞编码，保留为频率管理观察项。
8. Raw Annex-B 的 FFmpeg输入FPS可能显示25；阶段5必须由RTP/容器PTS明确30fps。

阶段 4 完成。当前进入阶段 5：RTP/RTSP 低延迟视频流。

## 8. 文档维护

- 重要状态同时更新本文件、`task_plan.md` 与 `progress.md`。
- 构建/启动/部署问题追加到 `orangepi5pro-kernel-troubleshooting.md`。
- 只记录代码、构建输出、日志或实机状态支持的事实；未验证的实现必须明确标注。
- 提交前运行 `git diff --check`；推送前 `git fetch origin` 并确认没有远端分叉。
