# 2. 快速运行与验收

<!-- camera-status-navigation -->
> 文档同步：2026-09-16。历史数据按原测试条件保留；当前阶段、环境、结果与待办统一见状态入口。
> 最新入口：[Camera 当前状态](../codex/CURRENT_STATUS.md)。
<!-- /camera-status-navigation -->

## 2.1 环境

| 项目 | 当前验证基线 |
| --- | --- |
| 板卡 | Orange Pi 5 Pro / RK3588S / 16 GiB |
| 内核 | `6.1.99-opi5pro-livecfg-baseline` |
| Sensor | OV13850 CAM2，I2C3 `0x10`，2-lane CSI-2 |
| Sensor 输出 | 2112x1568 SBGGR10，约 30 fps |
| ISP 输出 | `/dev/video11`，1920x1080 NV12，3,110,400 bytes/frame |
| 编码 | MPP H.264 CBR 8 Mbps，GOP 30，无 B 帧 |
| 网络 | RTP/UDP 5004 或 RTSP 8554 `/live` |

## 2.2 为什么要分成“配置”和“运行”

`V4L2Capture` 只验证 `/dev/video11` 已经是正确的 1920x1080 NV12，不在 C++ 里隐式修改整条 Media Pipeline。这使故障边界清楚：

- `configure_rkisp_1080p.sh` 失败：Sensor/DPHY/CSI/CIF/ISP 格式或节点问题。
- C++ `initialize()` 失败：设备节点、buffer 或库环境问题。
- 运行后失败：采集、编码、队列或网络问题。

## 2.3 编译入口

### 学习驱动

```bash
cd ~/linux-orangepi
OUT="$PWD/out/orangepi5pro-livecfg-baseline"

make O="$OUT" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  drivers/media/i2c/ov13850_i2c_min.o
```

此处仅检查驱动对象编译。活动基线要求`CONFIG_VIDEO_OV13850_I2C_MIN=y`并内建到
Image；历史上模块晚加载会错过CIF/ISP组图窗口。不要把`.ko`单独编译成功视为
媒体图验收，学习binding也不得抢占正式`ovti,ov13850`。

### RGA

```bash
make -C ov13850_opi5pro_learning/rga bundle
```

### MPP

```bash
make -C ov13850_opi5pro_learning/mpp bundle
```

`sdk` 目标会固定并构建官方 MPP 1.1.0；`bundle` 同时打包可执行文件与私有动态库。

### RTP/RTSP

WSL主机与板端采用不同架构的MPP SDK。优先使用已验证的一键入口：

```bash
# WSL源码根目录，要求固定MPP源码已存在
bash ov13850_opi5pro_learning/streaming/scripts/build_host.sh
# 板端解压后的学习工程目录
bash streaming/scripts/build_board.sh
```

下面的make示例适用于已经配好目标架构SDK的环境：

```bash
make -C ov13850_opi5pro_learning/streaming rtp
make -C ov13850_opi5pro_learning/streaming rtsp
```

### 性能计时

```bash
make -C ov13850_opi5pro_learning/benchmarks all
make -C ov13850_opi5pro_learning/benchmarks test
```

## 2.4 板端最小运行路径

带3A的推荐一键路径（当前release布局）：

```bash
cd ~/ov13850_opi5pro_learning/release/closeout-20260915/ov13850_opi5pro_learning
python3 streaming/scripts/camera_session.py --output ./evidence/live-new
```

默认使用已部署的runtime-v15，自动启动3A、配置ISP和RTSP；Ctrl+C清理本次进程。
下文手动流程用于分层调试，单独启动RTSP不会自动启动RKAIQ。

### 1. 确认活动驱动

```bash
uname -r
readlink -f /sys/bus/i2c/devices/3-0010/driver
cat /sys/bus/i2c/devices/3-0010/power/runtime_status
```

学习驱动应绑定到 `ov13850_i2c_min`。未采集时 PM 期望为 `suspended`。

### 2. 配置 Media Pipeline

```bash
cd ~/ov13850_opi5pro_learning
./scripts/configure_rkisp_1080p.sh
```

必须看到：

```text
Format OK: 1920x1080 NV12 stride=1920 size=3110400
CONFIGURATION_OK
```

### 3. 启动 RTSP 服务

```bash
./streaming/build/bin/v4l2_mpp_rtsp_server \
  --device /dev/video11 \
  --service 8554 \
  --mount /live \
  --bitrate 8000000 \
  --gop 30 \
  --mtu 1200 \
  --queue-buffers 2 \
  --mode dmabuf
```

服务端进入 GLib Main Loop 后应输出 `RTSP_SERVER_READY`。

### 4. Windows 接收

PowerShell 策略可能禁止未签名脚本，可在单次命令上显式使用 Bypass：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi\ov13850_opi5pro_learning\streaming\scripts\receive_h264_rtsp.ps1" `
  -Uri "rtsp://<board-ip>:8554/live" `
  -LatencyMs 30 `
  -Decoder auto
```

## 2.5 成功标准

- 客户端连接后服务端输出 `RTSP_CLIENT_CONNECTED`和 `IDR_REQUESTED`。
- 画面可立即解码，无持续花屏、卡顿或延迟单调增长。
- `frames_in` 持续增长，`timeouts=0`、`dropped=0`。
- 断开后重连可从新 IDR 恢复，不需重启 Camera/MPP。
- 结束后 Sensor PM 回到 `suspended` 且 runtime usage 为 0。

## 2.6 建议的隔离学习顺序

1. `rga_nv12_resize`：文件输入，只验证 RGA。
2. `nv12_mpp_encoder`：文件输入，只验证 MPP。
3. `v4l2_mpp_encoder`：加入实时 V4L2，但输出文件。
4. `v4l2_mpp_rtp_sender`：加入 GStreamer RTP/UDP。
5. `v4l2_mpp_rtsp_server`：再加入多线程、客户端生命周期与重连。
6. `run_rkaiq_local.sh`：最后将 3A 控制旁路接入已稳定的视频主链。
