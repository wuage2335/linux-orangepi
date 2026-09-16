# Camera 工程收口实施计划

<!-- camera-status-navigation -->
> 文档同步：2026-09-16。本文保留其标注日期的设计或验收条件，历史待办不等同于当前待办。
> 最新入口：[Camera 当前状态](../../codex/CURRENT_STATUS.md)。
<!-- /camera-status-navigation -->

Goal: 恢复完整构建、统一会话管理、执行真实回归并把证据写回交接入口。

Architecture: 保留现有 V4L2/MPP/GStreamer C++ 管线；增加 Python 标准库会话管理器，统一启动/停止与证据保存。WSL host SDK 与 aarch64 SDK 独立；板端部署在带日期 release 目录。

## 文件职责和验收

- [x] `streaming/scripts/build_host.sh`：验证 MPP commit，构建 host SDK、RTP/RTSP/benchmark，运行主机测试。
- [x] `streaming/scripts/build_board.sh`：板端架构检查、完整原生构建与主机可执行测试。
- [x] `streaming/scripts/camera_session.py`：锁、RKAIQ、配置、RTSP、信号、定时退出、可选双次解码、PM 和原始日志。
- [x] `streaming/tests/test_camera_session.py`：实际测试子进程的正常结束、错误结束与互斥。
- [x] 60s 冒烟、600s 固定曝光对照、1800s 3A 带解码长稳；保留所有日志。
- [ ] 根据实际状态测量光学延迟、CPU/RSS、温度与 DDR；缺测条件逐项标明。
- [x] `docs/codex/engineering_closeout_validation.md` 保存本轮环境、证据路径、指标与边界。
- [x] 更新 HANDOFF、README、PROJECT_MEMORY 和 progress。

执行方式：按用户明确授权在当前对话自主执行。涉及板端启动程序，仅关闭本次创建的子进程。
