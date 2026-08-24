# RGA NV12 File Resize Validation

日期：2026-08-24

## 1. 验证范围

本次验证只覆盖阶段 3 的文件式 RGA 最小闭环：

```text
RKISP 1920x1080 NV12 文件
        -> 官方 librga / imresize
        -> 1280x720 NV12 文件
```

它不覆盖 V4L2 实时取帧、DMA-BUF、缓存同步或摄像头端到端延迟。

## 2. 板端环境

- Board: Orange Pi 5 Pro, RK3588S
- Architecture: `aarch64`
- Kernel: `6.1.99-opi5pro-livecfg-baseline`
- RGA device: `/dev/rga`
- RGA driver: `RGA multicore Device Driver: v1.3.7`
- librga API: `1.10.6_[3]`
- RGA engines reported by librga: `RGA_2_Enhance RGA_3`
- librga SHA-256:
  `e150bda757fb5e8a649c429ec7cabaf851aa2a3be554ed494d5519e8790d943b`

`ldd` 证明程序实际加载部署包中的 `lib/librga.so`，不是板端系统目录中的其他
版本。最初测试脚本因字符串比较无法识别 `bin/../lib/librga.so` 与
`lib/librga.so` 是同一路径而误报；提交
`a8852cf24 test(rga): normalize bundled library path` 改为使用 `readlink -f`
规范化路径后，验收继续通过。

## 3. 输入证据

- Format: NV12
- Resolution: 1920x1080
- Size: 3,110,400 bytes
- File: `/tmp/rga-input-1920x1080-20260824_221358.nv12`
- SHA-256:
  `6915cb47a9a1fc49fd2b12e14b3f158334df7632e95aab143c365be28dfad15a`

输入来自已经验证的 RKISP mainpath，而不是人工生成的测试数据。

## 4. 黑盒验收

板端执行：

```bash
bash test_rga_nv12_resize.sh \
    rga-nv12-file-resize-bundle \
    /tmp/rga-input-1920x1080-20260824_221358.nv12
```

脚本验证：

- 无参数、缺失输入和短输入均明确失败；
- 输入输出同路径时拒绝执行且不损坏输入；
- 两次正确缩放均返回 `RGA_RESIZE_OK`；
- 两次输出均为 1,382,400 bytes；
- 相同输入的两次输出完全一致；
- 输出不是全零；
- librga 版本、5 次预热、100 次计时和性能字段完整。

最终结果：

```text
PASS: RGA NV12 resize tests
test_ret=0
```

## 5. 输出证据

- Format: NV12
- Resolution: 1280x720
- Size: 1,382,400 bytes
- File: `/tmp/rga-output-1280x720.nv12`
- SHA-256:
  `2f1d2bc1fbbe822ade7536d39f198b9affe4bf45fbd898b59735f0041a5d0deb`

平面统计：

```text
Y  bytes=921600 min=4   max=29
UV bytes=460800 min=123 max=128
```

Y/UV 平面尺寸均正确且不是空数据。亮度范围偏低，符合本次暗场输入；该现象不
构成 RGA 缩放失败。

## 6. 性能证据

每次程序运行先预热 5 次，再统计 100 次同步 `imresize()`。观测结果：

| Run | Total (us) | Average (us) | Operations/s |
| --- | ---: | ---: | ---: |
| Black-box A | 162,863.27 | 1,628.63 | 614.01 |
| Black-box B | 201,319.06 | 2,013.19 | 496.72 |
| Permanent output | 245,325.20 | 2,453.25 | 407.62 |

观测范围为约 1.63-2.45 ms/次，约 408-614 次/秒。即使按本次最慢值估算，纯
RGA resize 吞吐仍高于 30 fps 的处理需求。

这些数据不包含文件 I/O、V4L2 排队、sensor 曝光、ISP 或显示/编码，因此不得
表述为端到端摄像头延迟或最终系统 FPS。

## 7. 内核日志

以测试前 dmesg 行数为基线，只检查本次运行新增日志：

```text
no new RGA/MMU/IOMMU messages
RGA_FAULT_CHECK=OK
```

未观察到新增 RGA error、MMU fault 或 IOMMU fault。

## 8. 结论与边界

文件式 RGA 最小闭环通过：官方 librga 1.10.6_[3] 能在当前 RK3588S/v1.3.7
驱动环境中，把 RKISP 产生的 1920x1080 NV12 正确缩放为 1280x720 NV12。

阶段 3 的下一项是单独设计 V4L2 实时帧接入 RGA。DMA-BUF 零拷贝不是当前
文件实验的组成部分，应在后续低延迟优化中独立验证。
