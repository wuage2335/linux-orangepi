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
17. [`camera_pipeline_quantitative_results.md`](camera_pipeline_quantitative_results.md)：阶段 0–5 量化结果、公式、测量命令与结论边界。

## 当前项目主线

```text
OV13850/MIPI -> CSI/DPHY -> CIF/ISP -> V4L2
-> RGA（按需）-> MPP -> RTP/RTSP -> PC 播放
```

阶段 0-4 已完成，当前处于阶段 5“低延迟视频流”。

阶段 2 学习驱动已完成 controls、runtime PM、双模式、TRY/ACTIVE、stream
lifecycle、内建启动和实机验收。阶段 3 已完成 RKISP 1920x1080 NV12@30、
文件式 RGA、实时 copy/direct 两种路径，以及 bypass/copy/direct CPU/吞吐/RSS
对比。所有完成结论均有上述 validation 文档和 HANDOFF 原始证据支持。

阶段 4 已完成官方 MPP、自定义 H.264/H.265、实时 copy 和 DMA-BUF。阶段 5
已经完成 1080p30 RTP/UDP 实时里程碑：1800 帧 30.05 fps、0 timeout/drop/
queue overrun，Windows D3D11 稳定显示，三次端到端延迟为70–100ms。Stage 5
Task 8 packet timing与参数矩阵也已完成，推荐jitter30ms、GOP30、queue2。
Stage 5仍为当前阶段，后续是shared RTSP server和断线重连。

## 协作约束

这是用户的学习项目。Codex 默认负责讲解、拆分、审查和诊断，用户是主要实践者。未经用户明确授权，不得一次性代写完整阶段或大批量代码。任何完成结论必须附带构建或实机验证证据。

## 文档维护

- handoff 的仓库入口固定为 `docs/codex/HANDOFF.md`。
- 只记录由代码、日志、构建输出或实机状态证实的事实；推断必须明确标注。
- 重要进展同时更新 `HANDOFF.md` 和 `progress.md`。
- 新增内核故障按 troubleshooting 文档末尾模板记录。
- 提交前执行 `git diff --check`，并检查 Markdown 相对链接和 UTF-8 编码。
