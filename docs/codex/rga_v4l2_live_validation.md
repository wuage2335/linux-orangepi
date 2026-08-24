# V4L2 Real-Time RGA Copy Path Validation

日期：2026-08-24

## 1. 验证范围

验证第一版实时 copy path：

```text
RKISP /dev/video11 1920x1080 NV12
        -> V4L2 MMAP DQBUF
        -> memcpy 到复用源内存
        -> 立即 QBUF
        -> RGA imresize
        -> 1280x720 NV12
```

程序预丢弃 3 帧，正式处理 300 帧，保存最后一帧。本次不涉及直接导入 MMAP
地址或 DMA-BUF。

## 2. 环境与部署

- Board: Orange Pi 5 Pro, RK3588S
- Architecture: `aarch64`
- Kernel: `6.1.99-opi5pro-livecfg-baseline`
- Video node: `/dev/video11`, `rkisp_mainpath`
- RGA driver: multicore v1.3.7（沿用同次阶段 3 基线）
- librga API: `1.10.6_[3]`
- Deploy archive SHA-256:
  `2605f763c4692f6806bae0f0acd0fd432231ec7ce66c35e667ff28b3021e8f5c`

运行前 `/dev/video11` 为 2112x1568 NV12。执行
`configure_rkisp_1080p.sh` 后回读确认：

```text
Width/Height   : 1920/1080
Pixel Format   : NV12
Number planes  : 1
Bytes per Line : 1920
Size Image     : 3110400
CONFIGURATION_OK
```

实时程序只校验格式，不主动修改 media graph。

## 3. 黑盒验收结果

板端执行：

```bash
bash test_rga_v4l2_live.sh \
    rga-bundle \
    /dev/video11 \
    /tmp/rga-live-last-1280x720.nv12
```

测试先确认无参数和 `/dev/null` 设备均以 `ERROR:` 失败，再运行正向 300 帧。

核心输出：

```text
input=1920x1080 NV12 bytes=3110400
output=1280x720 NV12 bytes=1382400
pre_skipped=3 processed=300 timeouts=0 dropped=0
copy_total_us=219392.64 copy_average_us=731.31
rga_total_us=777816.43 rga_average_us=2592.72
loop_total_s=9.99 capture_process_fps=30.04
RGA_V4L2_LIVE_OK
PASS: realtime V4L2 RGA copy-path tests
test_ret=0
```

## 4. 输出证据

- File: `/tmp/rga-live-last-1280x720.nv12`
- Format: NV12
- Resolution: 1280x720
- Size: 1,382,400 bytes
- SHA-256:
  `5b11f6f24b6469f361821397ba986f43bb450d8a2965a2f9d91fcd4b4e26f68b`

平面统计：

```text
Y  bytes=921600 min=5  max=255
UV bytes=460800 min=90 max=130
```

Y/UV 平面大小正确且范围有效。

## 5. 性能解释

- 显式 memcpy 平均耗时：0.731 ms/帧；
- 同步 RGA resize 平均耗时：2.593 ms/帧；
- copy + RGA 平均约 3.324 ms/帧；
- 完整 capture/process loop：30.04 fps；
- V4L2 sequence drop：0；
- poll timeout：0。

loop FPS 包含等待 V4L2 帧、DQBUF/QBUF、memcpy 和 RGA，但不包含初始化、预丢弃
和最后写盘。30.04 fps 说明该 copy path 能跟上当前 sensor 30 fps 输出；它仍不
等于显示、编码或网络端到端延迟。

## 6. PM 与内核日志

程序退出并等待 1 秒后：

```text
/sys/bus/i2c/devices/3-0010/power/runtime_status = suspended
/sys/bus/i2c/devices/3-0010/power/runtime_usage  = 0
```

新增内核日志包含正常的 CIF/CSI/D-PHY/sensor stream on/off。未观察到新增
CIF/ISP/RGA/MMU/IOMMU error、fault、timeout 或 overflow：

```text
LIVE_FAULT_CHECK=OK
```

## 7. 结论与下一步

第一版 `V4L2 MMAP -> memcpy -> RGA` 实时路径通过。它证明 capture buffer 在
DQBUF 后复制、立即 QBUF，再由独立源内存执行 RGA 的所有权顺序正确，并能稳定
处理 300 帧 1920x1080 NV12@30。

下一项是在相同 3+300 帧契约下设计 direct-MMAP import 版本，比较显式 copy 与
直接导入路径的 CPU 占用和时延。DMA-BUF 继续留到低延迟优化阶段。
