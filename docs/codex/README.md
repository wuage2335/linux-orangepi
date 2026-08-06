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

## 当前项目主线

```text
OV13850/MIPI -> CSI/DPHY -> CIF/ISP -> V4L2
-> RGA（按需）-> MPP -> RTP/RTSP -> PC 播放
```

当前处于阶段 2“V4L2 驱动完善”。阶段 0“基线验证”和阶段 1“传感器与 DTS”此前已完成，不应无依据重复执行。

当前执行状态：学习驱动 2A 已写入一部分静态脚手架（V4L2 control/PM 头文件、
control 状态字段、寄存器常量、菜单数据，并移除竞争性的
`i2c:ovti,ov13850` alias）。阶段 2 代码尚未完成，且按用户要求尚未构建、
加载模块或上机验证；不能据此宣称驱动可用。

## 协作约束

这是用户的学习项目。Codex 默认负责讲解、拆分、审查和诊断，用户是主要实践者。未经用户明确授权，不得一次性代写完整阶段或大批量代码。任何完成结论必须附带构建或实机验证证据。

## 文档维护

- handoff 的仓库入口固定为 `docs/codex/HANDOFF.md`。
- 只记录由代码、日志、构建输出或实机状态证实的事实；推断必须明确标注。
- 重要进展同时更新 `HANDOFF.md` 和 `progress.md`。
- 新增内核故障按 troubleshooting 文档末尾模板记录。
- 提交前执行 `git diff --check`，并检查 Markdown 相对链接和 UTF-8 编码。
