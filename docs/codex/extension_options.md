# Camera 项目扩展备选（尚未选定实施）

<!-- camera-status-navigation -->
> 文档同步：2026-09-16。历史数据按原测试条件保留；当前阶段、环境、结果与待办统一见状态入口。
> 最新入口：[Camera 当前状态](CURRENT_STATUS.md)。
<!-- /camera-status-navigation -->

用户希望融合其他技术栈；以下是已讨论的候选方向，不是已经实现的能力。

| 方向 | 可交付功能 | 主要技术栈 | 需要解决的关键问题 |
| --- | --- | --- | --- |
| AI事件相机 | 检测、跟踪、区域事件、预录像与回放 | RKNN/RGA、ByteTrack、SQLite、MQTT、Web前端 | 多消费者buffer生命周期、推理背压、画面/结果时间对齐、关键帧切片 |
| WebRTC视频平台 | 浏览器观看、码率控制、多客户端 | MediaMTX、WebRTC、TypeScript、Go或Rust | 浏览器H.264兼容、NAT、重连、码率调节与权限 |
| ROS2视觉云台 | 检测目标并控制云台，记录闭环数据 | ROS2、RKNN、STM32/FreeRTOS、CAN/UART、Foxglove | 标定、时间同步、视觉延迟、丢失目标与控制保护 |
| Rust多媒体服务 | 配置API、状态机、恢复与观测面板 | C++数据处理、Rust/Tokio/Axum、gRPC、OpenTelemetry | 命令在线程安全时机生效、资源生命周期、版本化发布 |
| 语义视频检索 | 用文本搜索录像片段 | CLIP/OpenCLIP、Qdrant、Python、Web前端 | 抽帧、事件去重、时间定位、模型精度与算力分配 |

建议组合路线：浏览器观看 → NPU检测/跟踪 → 事件录像 → 服务化管理；如重点转向
机器人，可选择ROS2与STM32云台闭环。具体范围和验收目标仍待用户选择。

## 已查阅的上游参考

- [RKNN Model Zoo](https://github.com/airockchip/rknn_model_zoo)
- [ByteTrack](https://github.com/FoundationVision/ByteTrack)
- [Frigate](https://github.com/blakeblackshear/frigate)
- [MediaMTX](https://github.com/bluenviron/mediamtx)
- [ROS2 Control](https://github.com/ros-controls/ros2_control)
- [Foxglove SDK](https://github.com/foxglove/foxglove-sdk)
- [Axum](https://github.com/tokio-rs/axum)
- [gRPC Rust](https://github.com/grpc/grpc-rust)
- [OpenTelemetry C++](https://github.com/open-telemetry/opentelemetry-cpp)
- [OpenCLIP](https://github.com/mlfoundations/open_clip)
- [Qdrant](https://github.com/qdrant/qdrant)

上游示例和性能表不代表已在本板完成集成；实际引入时应固定版本并单独验证。
