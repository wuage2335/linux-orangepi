# V4L2 RGA Direct-MMAP Comparison Validation

日期：2026-08-25

## 1. 范围

在相同 RKISP 1920x1080 NV12、3 帧预丢弃和 300 帧正式循环下比较：

```text
bypass : V4L2 -> /dev/null
copy   : DQBUF -> memcpy -> QBUF -> RGA
direct : DQBUF -> RGA -> QBUF
```

Direct 使用 `importbuffer_virtualaddr()` 在 STREAMON 前一次性导入 4 个 MMAP
地址，不是 DMA-BUF。

## 2. 环境

- Board: Orange Pi 5 Pro, RK3588S
- Kernel: `6.1.99-opi5pro-livecfg-baseline`
- Video: `/dev/video11`, `rkisp_mainpath`
- Format: 1920x1080 NV12, stride 1920, size 3,110,400
- librga: 1.10.6_[3]
- Deploy SHA-256:
  `d3d50f9d50554c9130d5474dad7c1146d9066ea2c78823b15042c02d583c3906`
- GNU time: 1.9, 板端测试工具依赖

## 3. 联合黑盒测试

Copy：

```text
mode=copy
processed=300 timeouts=0 dropped=0
copy_average_us=1065.31
rga_average_us=2754.20
capture_process_fps=30.04
cpu_user_ms=332.64
cpu_system_ms=377.74
process_cpu_percent=7.11
```

Direct：

```text
mode=direct
processed=300 timeouts=0 dropped=0
copy_average_us=0.00
rga_average_us=2528.98
capture_process_fps=30.04
cpu_user_ms=18.50
cpu_system_ms=245.23
process_cpu_percent=2.64
```

结果：

```text
PASS: realtime V4L2 RGA copy/direct tests
test_ret=0
```

这一轮 direct 消除了 1.065 ms copy，并把进程 CPU 从 7.11% 降到 2.64%。

## 4. 三路径 GNU time Benchmark

| Path | FPS | Internal CPU | Copy avg | RGA avg | User time | System time | External CPU | Elapsed | Max RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Bypass | 约 30 | N/A | N/A | N/A | 0.01 s | 0.13 s | 1% | 10.24 s | 12,844 KB |
| Copy | 30.04 | 4.95% | 692.86 us | 2,579.86 us | 0.22 s | 0.31 s | 5% | 10.33 s | 19,564 KB |
| Direct | 30.04 | 5.41% | 0 | 3,174.07 us | 0.02 s | 0.56 s | 5% | 10.33 s | 16,380 KB |

Benchmark 结果：

- bypass CPU/RSS 最低，说明不需要额外变换时应绕过 RGA；
- direct 确定移除了 memcpy，并比 copy 少约 3.1 MB source buffer，RSS 低
  3,184 KB；
- direct 用户态 CPU 显著较低，但该轮 system CPU 较高；
- direct 总进程 CPU 5.41% 略高于 copy 4.95%，与联合测试中 direct 更低的结果
  不一致，说明短时 CPU 与 RGA 调度存在波动；
- direct RGA 平均耗时该轮也高于 copy，不能声称 direct 必然缩短 RGA 时间；
- 三条路径都受 30 fps sensor 节奏限制，吞吐相同。

结论只确认 direct 去除显式 copy 和降低内存占用；总 CPU/处理时间需多轮统计才可
给出稳定优势结论。

## 5. 输出

Copy：

```text
size=1382400
SHA-256=36db7c5f6d96c45008c032eaa5d6c79366512d4934e7d212cc2f108f4bcdb987
Y  bytes=921600 min=4 max=255
UV bytes=460800 min=91 max=130
```

Direct：

```text
size=1382400
SHA-256=d16f388a8882abae79aff68abb0e87098937885e1af62acc26e1c65728692a06
Y  bytes=921600 min=5 max=255
UV bytes=460800 min=90 max=130
```

两次采集发生在不同时间，不要求 SHA 相同；两份 NV12 平面大小和数值范围均
有效。

## 6. PM 与日志

测试退出后：

```text
runtime_status=suspended
runtime_usage=0
DIRECT_FAULT_CHECK=OK
```

未观察到新增 CIF/ISP/RGA/MMU/IOMMU fault、timeout 或 overflow。

## 7. 阶段 3 结论

阶段 3 功能与对比目标完成：RAW->RKISP->NV12、文件 RGA、实时 copy、实时
direct 和 bypass/copy/direct CPU/吞吐比较都有实机证据。

旋转和色彩转换当前没有业务需求，依据“按需使用 RGA”不额外实现。DMA-BUF
留到后续低延迟优化。下一阶段进入 RK MPP H.264/H.265 硬件编码。
