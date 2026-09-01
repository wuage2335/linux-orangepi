# RK3588 Camera 项目 Codex 文档入口

本目录用于让新的 Codex 对话在 `linux-orangepi` 仓库内直接恢复项目上下文，不依赖 Windows 本地工作区或旧聊天记录。

## 推荐阅读顺序

1. [`HANDOFF.md`](HANDOFF.md)：当前状态、环境、已验证事实、阻塞项和下一步。
2. [`task_plan.md`](task_plan.md)：项目阶段、当前阶段和学习协作原则。
3. [`progress.md`](progress.md)：按日期记录已经执行的工作和验证结果。
4. [`findings.md`](findings.md)：源码阅读、驱动审查和环境发现。
5. [`RK3588摄像头低延迟链路项目计划.md`](RK3588摄像头低延迟链路项目计划.md)：完整技术路线和各阶段验收标准。
6. [`orangepi5pro-kernel-troubleshooting.md`](orangepi5pro-kernel-troubleshooting.md)：Orange Pi 5 Pro 内核构建、启动、模块、部署和回滚问题手册。
7. [`../superpowers/specs/2026-08-03-ov13850-stage2-learning-driver-design.md`](../superpowers/specs/2026-08-03-ov13850-stage2-learning-driver-design.md)：已确认的学习驱动阶段 2 设计边界。
8. [`../superpowers/plans/2026-08-03-ov13850-stage2-learning-driver-plan.md`](../superpowers/plans/2026-08-03-ov13850-stage2-learning-driver-plan.md)：逐函数的阶段 2 实施与统一验证计划。
9. [`rga_nv12_file_resize_validation.md`](rga_nv12_file_resize_validation.md)：文件式 RGA resize 实机证据。
10. [`rga_v4l2_live_validation.md`](rga_v4l2_live_validation.md)：V4L2 MMAP + memcpy + RGA 实时证据。
11. [`rga_v4l2_direct_comparison_validation.md`](rga_v4l2_direct_comparison_validation.md)：bypass/copy/direct CPU、吞吐与 RSS 对比。
12. [`mpp_official_environment_validation.md`](mpp_official_environment_validation.md)：官方 MPP 1.1.0 与 RKVENC 环境验证。
13. [`mpp_file_encoding_validation.md`](mpp_file_encoding_validation.md)：H.264/H.265 文件编码与参数矩阵。
14. [`mpp_live_encoding_validation.md`](mpp_live_encoding_validation.md)：V4L2 实时 H.264 copy/DMA-BUF 证据。
15. [`mpp_dmabuf_feasibility.md`](mpp_dmabuf_feasibility.md)：EXPBUF、EXT_DMA、stride 和所有权结论。
16. [`stage5_rtp_streaming_validation.md`](stage5_rtp_streaming_validation.md)：实时 RTP、Windows D3D11 播放和端到端延迟证据。
17. [`stage5_rtsp_recovery_validation.md`](stage5_rtsp_recovery_validation.md)：shared RTSP、实时 PTS 修复、重连与五组延迟证据。
18. [`camera_pipeline_quantitative_results.md`](camera_pipeline_quantitative_results.md)：阶段 0–6 量化结果、公式、测量命令与结论边界。
19. [`stage6_rkaiq_3a_validation.md`](stage6_rkaiq_3a_validation.md)：RKAIQ/3A 接入、兼容修复、动态 stats 和真实三场景验证。
20. [`interview_project_star_and_improvements.md`](interview_project_star_and_improvements.md)：面试 STAR 讲述稿、简历 bullets、项目不足和优化路线。

## 当前项目主线

```text
OV13850/MIPI -> CSI/DPHY -> CIF/ISP -> V4L2
-> RGA（按需）-> MPP -> RTP/RTSP -> PC 播放
```

阶段0-6已完成；阶段7“AI感知与业务扩展”为可选下一阶段。

阶段 2 学习驱动已完成 controls、runtime PM、双模式、TRY/ACTIVE、stream
lifecycle、内建启动和实机验收。阶段 3 已完成 RKISP 1920x1080 NV12@30、
文件式 RGA、实时 copy/direct 两种路径，以及 bypass/copy/direct CPU/吞吐/RSS
对比。所有完成结论均有上述 validation 文档和 HANDOFF 原始证据支持。

阶段 4 已完成官方 MPP、自定义 H.264/H.265、实时 copy 和 DMA-BUF。阶段 5
已经完成 1080p30 RTP/UDP 实时里程碑：1800 帧 30.05 fps、0 timeout/drop/
queue overrun，Windows D3D11 稳定显示，三次端到端延迟为70–100ms。Stage 5
Task 8 packet timing与参数矩阵推荐jitter30ms、GOP30、queue2。Task 9 shared
RTSP、实时时钟PTS、GStreamer/VLC重连和阶段5总回归也已完成。

Stage 6 已完成私有 RKAIQ、module-info、IQ、固定焦点、单摄 online、动态 stats
和非root AE/AWB算法验证；明亮/普通/较暗三种实景画质、3A性能以及持续稳定性
已经验收。正常实景3A精确同屏延迟、DDR带宽和温度没有同轮测量，明确作为可选
补测，不虚构结果，也不阻塞Stage 6关闭。收口结论见
[`stage6_rkaiq_3a_validation.md`](stage6_rkaiq_3a_validation.md)。

## 快速视频传输：Orange Pi 到 Windows

默认链路为 `OV13850 -> RKISP -> MPP H.264 -> RTSP/TCP -> Windows GStreamer`。
下面的命令使用历史验证过的板端目录和默认 RTSP 地址；若 DHCP 改变了板卡 IP，
只替换 Windows 命令中的 `<BOARD_IP>`。

### 1. Orange Pi：启动 3A、配置 ISP、启动 RTSP

在 Orange Pi 的一个终端中执行：

```bash
STREAM_ROOT=~/ov13850_opi5pro_learning/stage5/task7-live-rtp
AIQ_BUNDLE=~/ov13850_opi5pro_learning/stage6/rkaiq-3a/runtime-v15

"$AIQ_BUNDLE/bin/run_rkaiq_local.sh" \
  > /tmp/ov13850-rkaiq.log 2>&1 &
AIQ_PID=$!

sleep 3
"$STREAM_ROOT/configure_rkisp_1080p.sh"

"$STREAM_ROOT/streaming/build/bin/v4l2_mpp_rtsp_server" \
  --device /dev/video11 \
  --service 8554 \
  --mount /live \
  --bitrate 8000000 \
  --gop 30 \
  --mtu 1200 \
  --queue-buffers 2 \
  --mode dmabuf
```

`run_rkaiq_local.sh` 让 AE/AWB 生效，改善固定曝光造成的偏暗偏绿；
`configure_rkisp_1080p.sh` 固定 RKISP 为 1920x1080 NV12；最后一个程序持续提供
`rtsp://<BOARD_IP>:8554/live`。服务端运行期间保持这个终端不要关闭。

按 `Ctrl+C` 停止 RTSP 后，在同一个终端执行：

```bash
kill "$AIQ_PID"
wait "$AIQ_PID"
```

### 2. Windows PowerShell：接收并显示

先将 `<BOARD_IP>` 替换为 Orange Pi 当前 Wi-Fi 地址，例如历史地址
`192.168.1.10`。以下命令直接调用 GStreamer，不依赖 PowerShell 执行项目脚本：

```powershell
$gstBin = Join-Path $env:LOCALAPPDATA 'Programs\gstreamer\1.0\msvc_x86_64\bin'
$env:Path = "$gstBin;$env:Path"

gst-launch-1.0 -v `
  rtspsrc location='rtsp://<BOARD_IP>:8554/live' latency=30 protocols=tcp drop-on-latency=true `
  ! rtph264depay `
  ! h264parse `
  ! d3d11h264dec `
  ! d3d11videosink sync=false
```

若 Windows 没有 `d3d11h264dec`，使用软件解码版本：

```powershell
gst-launch-1.0 -v `
  rtspsrc location='rtsp://<BOARD_IP>:8554/live' latency=30 protocols=tcp drop-on-latency=true `
  ! rtph264depay `
  ! h264parse `
  ! avdec_h264 `
  ! autovideosink sync=false
```

项目也提供 [`receive_h264_rtsp.ps1`](../../ov13850_opi5pro_learning/streaming/scripts/receive_h264_rtsp.ps1)，
会自动选择硬件或软件解码；但 PowerShell 策略可能要求脚本签名。上面的直接
`gst-launch-1.0` 命令是更稳定的首次验收入口。

### 3. 成功标志与排查顺序

- Orange Pi 输出 `RTSP_SERVER_READY`，随后客户端连接时出现
  `RTSP_CLIENT_CONNECTED` 和 `IDR_REQUESTED`。
- Windows 弹出视频窗口，首帧应在很短时间内显示；正常运行时没有持续花屏或
  延迟累积。
- Windows 无画面时，依次检查板端 `ip -br addr`、`/dev/video11`、RTSP 服务端
  日志，再检查 Windows 是否已找到 `gst-launch-1.0` 和 `d3d11h264dec`。

## 协作约束

这是用户的学习项目。Codex 默认负责讲解、拆分、审查和诊断，用户是主要实践者。未经用户明确授权，不得一次性代写完整阶段或大批量代码。任何完成结论必须附带构建或实机验证证据。

## 文档维护

- handoff 的仓库入口固定为 `docs/codex/HANDOFF.md`。
- 只记录由代码、日志、构建输出或实机状态证实的事实；推断必须明确标注。
- 重要进展同时更新 `HANDOFF.md` 和 `progress.md`。
- 新增内核故障按 troubleshooting 文档末尾模板记录。
- 提交前执行 `git diff --check`，并检查 Markdown 相对链接和 UTF-8 编码。
