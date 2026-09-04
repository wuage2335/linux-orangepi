# 6. 分层调试手册与已知风险

## 6.1 调试总原则

不要看到“没画面”就直接改 Sensor 寄存器。按数据流顺序找到第一个异常层：

```text
1. DT / driver binding
2. Sensor power / I2C / chip ID
3. Media graph / RAW format
4. CIF / RKISP / V4L2 capture
5. NV12 layout and brightness
6. RGA or MPP
7. GStreamer queue / RTP / RTSP
8. Windows receive / decode / display
9. RKAIQ 3A and IQ quality
```

## 6.2 只有 `lo`，SSH 失联

**症状**：新内核可启动，`/sys/class/net` 只有 `lo`，SSH 服务虽 active 但无 IP。

**已验证原因**：新 kernel release 没有对应 `/lib/modules/<release>` 和 `bcmdhd.ko`，板载 Wi-Fi 驱动未加载。

**检查**：`uname -r`、`ls /lib/modules/$(uname -r)`、`modinfo bcmdhd`、`ip -br link`、`dmesg | grep -i dhd`。

**解决**：在同一 `O=` 和同一 kernelrelease 下构建/INSTALL_MOD_PATH 部署模块，`depmod -a`，不要只替换 `Image`。

## 6.3 Sensor 节点存在但未绑定学习驱动

**检查**：

```bash
tr '\0' '\n' < /proc/device-tree/i2c@feab0000/ov13850-2@10/compatible
readlink -f /sys/bus/i2c/devices/3-0010/driver
lsmod | grep ov13850
```

**分层判断**：

- compatible 仍为 `ovti,ov13850`：学习 overlay 未生效。
- compatible 正确但 unbound：模块未加载、alias/Kconfig 或 probe 失败。
- 绑到正式 `ov13850`：两套 binding 没有隔离好。

学习驱动只允许 `learning,ov13850-i2c`，这是防止抢占正式驱动的安全边界。

## 6.4 Chip ID 读取 `000000` 或冷启动首次失败

**可能原因**：时钟未启用、reset/PWDN 极性错、供电未稳定、I2C 地址错、第一次交易时间过早。

**代码入口**：`ov13850_min_power_on()`、`ov13850_min_read_reg()`、`ov13850_min_probe()`。

**项目经验**：正式驱动的冷启动 ID 失败曾通过资源清理后返回 `-EPROBE_DEFER` 解决，让内核在依赖准备好后重试；不应用无限循环在 probe 中死等。

## 6.5 `CONFIGURATION_OK` 前失败

**症状**：`configure_rkisp_1080p.sh` 找不到节点或 format/crop readback 不一致。

**检查**：

1. `media-ctl -p` 确认 Sensor → D-PHY → CSI → CIF → ISP 链路闭合。
2. `/sys/class/video4linux/*/name` 确认动态节点名，不要凭记忆硬编 `/dev/v4l-subdevN`。
3. 所有 RAW pad 都是 2112x1568 `SBGGR10`。
4. mainpath 是 1920x1080 NV12，stride 1920，size 3,110,400。
5. crop 是 left 0、top 190、width 2112、height 1188。

## 6.6 V4L2 `poll timeout`

**代码位置**：`V4L2Capture::dequeue()`。

**可能原因**：Sensor 没有 stream-on、Media Graph 不闭合、格式不一致、MIPI 错误、buffer 没有 QBUF、上游 PM 被错误挂起。

**调试顺序**：先用 `v4l2-ctl --stream-mmap --stream-count=30 --stream-to=/dev/null --stream-poll`脱离 MPP/GStreamer 验证采集，再看 `dmesg` 的 CSI/CIF/ISP fault。

## 6.7 MPP 编码成功但颜色或图像错乱

**高概率原因**：stride/UV offset 声明错误或 DMA-BUF 过早 QBUF。

- copy 路径的内部 buffer 是 1920x1088，UV 起点在 1088 行后。
- V4L2 导出 buffer 是紧凑 1920x1080，DMA-BUF 模式必须设 `ver_stride=1080`。
- DMA-BUF 路径必须在 `encode_get_packet()` 返回后再 QBUF。

对照文件：`v4l2_mpp_encoder.cpp` 的 copy/dmabuf 分支。

## 6.8 网络端一分钟后花屏、卡顿、延迟累积

**已验证根因**：RTSP 旧版 PTS 用固定 30.00 fps 计算，实际采集约 30.05 fps，每分钟累积约 95 ms 漂移。它不是发送 queue 无限增长；实测 queue 没有 overrun。

**修复**：`LivePtsClock` 使用真实 monotonic time 生成 RTSP 会话 PTS。

**验证**：至少 2 分钟观察延迟是否单调增加，不只看 10 秒短测；同时查 `dropped`、queue overrun、Send-Q 和客户端重连。

## 6.9 新 RTSP 客户端黑屏或先花屏

**原因**：从 GOP 中间加入，没有 SPS/PPS 或之前参考帧。

**代码解法**：`add_client()` 设 `header_pending_` 和 `idr_pending_`；worker 在下一帧前调 `request_idr()`；`consume()` 先推缓存 header。

## 6.10 网络卡顿后延迟越来越大

**检查**：队列是否有界，是否 `leaky=downstream`，接收端 jitter buffer 是否过大，播放器是否自行缓存。

当前推荐是 queue 2、GOP 30、MTU 1200、GStreamer jitter 30 ms。VLC 虽兼容，但实测默认延迟更高，不作低延迟基准。

## 6.11 画面偏暗偏绿

**首先隔离**：Sensor test pattern 的白/黄/青/绿/品红/红/蓝/黑是否正确。若 pattern 正确，则 NV12 格式和解码基本可排除，问题更可能在真实场景的 AE/AWB/IQ。

**检查**：

1. Sensor/module/lens 是否是 `ov13850 / CMK-CT0116 / default`。
2. IQ 文件是否匹配，旧 RKAIQ 解析器的键/枚举是否完成兼容转换。
3. RKAIQ 是否持续读 `/dev/video18` stats。
4. 曝光/增益/VBLANK 是否随环境改变。
5. AWB gain 是否每帧更新并下发 ISP。

## 6.12 RKAIQ 启动失败

| 症状 | 优先检查 |
| --- | --- |
| module-info ioctl 失败 | 活动内核是否有 `RKMODULE_GET_MODULE_INFO`；临时 shim 开关是否显式启用 |
| 找不到 IQ | sensor/module/lens 名称与 IQ 文件名 |
| IQ parse 空指针 | `prepare_compatible_iq.sh` 是否转换了旧解析器所需键 |
| 寻找 VCM/对焦失败 | CAM2 是固定焦，IQ 应设 fixed AF 并禁用 contrast AF |
| 普通用户实时调度失败 | 兼容 patch 应从 `SCHED_RR` 回退 `SCHED_OTHER` |

## 6.13 RGA 返回成功但不应声称“主链路已接入”

检查输出类型。当前 `DirectRgaResizer::output_` 是 `std::vector<unsigned char>`，最后写文件；没有 `MppBuffer`、`mpp_buffer_import()` 或 `encode_external_frame()`。因此它是独立实验，不是最终 RTSP 主链的固定一环。

## 6.14 内核驱动调试风险

- sysfs `reg_addr/reg_value/array_test/mode_apply/full_init` 是学习与 bring-up 接口，可直接改寄存器，不适合无权限的生产接口。
- `v4l2-compliance` 仍有 control event subscription 遗留，不应声称 100% compliance。
- `set_fmt()` 只会在两个离散模式中选最近值，不是 Sensor 内部任意缩放。
- 调试信息 `dev_info` 较多，高频使用前应收敛或改成 debug 级别。

## 6.15 用户态已知设计边界

- `V4L2Capture` 硬验证 1920x1080 NV12，不是通用 Camera class。
- MPP 核心编译期固定 1920x1080/30 fps；要支持任意尺寸需要重构 config 和 buffer 计算。
- RTSP 下游只是 H.264，虽然文件 encoder 支持 H.265。
- MPP packet 到 GstBuffer 仍有压缩数据 copy；不能称全链路零拷贝。
- RTSP 采集工作线程与 MPP 是阻塞串行；若未来引入异步 MPP，必须新增 buffer fence/所有权跟踪。
- 当前没有同轮、同时基的板端到 Windows 每段时间戳，网络/播放子段只能用端到端同屏样本和间接指标推理。
