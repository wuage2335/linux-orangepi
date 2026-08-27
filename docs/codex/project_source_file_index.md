# 摄像头学习项目源文件索引

本文只索引项目新增或重点修改的源文件，便于按执行链路阅读；不重复罗列验证文档、
截图、编译产物和第三方头文件。

## Sensor与V4L2

- `drivers/media/i2c/ov13850_i2c_min.c`：OV13850学习驱动；包含寄存器表、controls、
  runtime PM、双模式协商和stream lifecycle。
- `drivers/media/i2c/ov13850.c`：正式参考驱动，只作Rockchip接口和行为对照。

## RKISP配置

- `ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh`：把sensor、DPHY、
  CSI/CIF和RKISP mainpath统一配置为2112x1568 RAW10输入、1920x1080 NV12输出。

## RGA

- `ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp`：文件式NV12硬件缩放。
- `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp`：V4L2实时copy/direct RGA路径。

## MPP

- `ov13850_opi5pro_learning/mpp/src/encoded_packet_sink.hpp`：编码包的统一sink契约。
- `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp`：共享MPP H.264/H.265编码核心。
- `ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp`：MMAP/EXPBUF采集和DMA-BUF所有权。
- `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`：文件NV12编码前端。
- `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`：实时copy/DMA-BUF编码前端。

## RTP与RTSP

- `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.hpp/.cpp`：appsrc到RTP/UDP。
- `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`：实时DMA-BUF RTP发送。
- `ov13850_opi5pro_learning/streaming/src/congestion_idr_controller.hpp`：queue overrun
  的IDR冷却和合并策略。
- `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.hpp/.cpp`：shared media
  factory、appsrc生命周期、客户端计数和bus error处理。
- `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp`：GLib主循环与唯一
  V4L2/MPP worker的组合入口。
- `ov13850_opi5pro_learning/streaming/src/live_pts_clock.hpp`：按真实单调时间生成
  RTSP PTS，消除30.05fps与名义30fps造成的长期漂移。

## 推荐阅读顺序

```text
ov13850_i2c_min.c
-> configure_rkisp_1080p.sh
-> v4l2_capture.hpp
-> mpp_encoder_core.hpp
-> gst_rtp_sink.cpp / gst_rtsp_server.cpp
-> v4l2_mpp_rtp_sender.cpp / v4l2_mpp_rtsp_server.cpp
```
