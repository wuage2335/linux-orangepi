# 10. 实时时序、实测结果和测量方法

## 10.1 先区分四种“时间”

| 类型 | 例子 | 能否直接相加 |
| --- | --- | --- |
| 帧周期 | 相邻 V4L2 timestamp 约 33.28 ms | 是节拍，不是单模块处理耗时 |
| 同步函数耗时 | MPP `encode_*` 约 4.8 ms | 可用于定位 CPU/硬件调用 |
| 队列等待 | jitter buffer、播放刷新 | 会影响端到端，但不是板端函数时间 |
| 端到端延迟 | 光线到 PC 屏幕 | 需要同屏/统一时钟实测，不能用局部时间简单伪造 |

## 10.2 固定 1080p30 主链实测

环境：5 轮 x 300 帧，每轮 warm-up 30，Sensor test pattern 1，exposure 1000、gain 16、VBLANK 96，H.264 CBR 8 Mbps、GOP 30、4 V4L2 buffers。

| 阶段 | Mean | P50 | P95 | P99 | 边界 |
| --- | ---: | ---: | ---: | ---: | --- |
| 帧周期 | 33.280 ms | 33.280 ms | 33.306 ms | 33.354 ms | 相邻内核 timestamp |
| RKISP SOF → mainpath done | 27 ms | 27 ms | 27 ms | 27 ms | 包含 Sensor 读出重叠、ISP 和 DMA |
| SOF → DQBUF 返回 | 28.482 ms | 28.501 ms | 28.602 ms | 28.627 ms | ISP + vb2 唤醒/调度 |
| 3.11 MB CPU copy | 1.936 ms | 2.030 ms | 2.089 ms | 3.086 ms | `load_nv12()` + padding |
| MPP DMA-BUF 编码 | 4.780 ms | 4.832 ms | 4.962 ms | 5.022 ms | submit 到 packet 返回 |
| DMA post-DQ 关键区 | 约 5.230 ms | 5.358 ms | 5.522 ms | 见原始结果 | DQBUF 到 MPP 完成 + QBUF |
| copy post-DQ 关键区 | 约 6.872 ms | 7.020 ms | 7.132 ms | 见原始结果 | 多一次整帧 copy |
| 真实 8 Mbps GStreamer push | 66.45 us | 60.67 us | 89.83 us | 见原始结果 | MPP packet copy + appsrc 入队 |
| DQBUF → RTP queue | 见原始统计 | 4.546 ms | - | - | 板端编码+入队口径 |
| SOF → RTP queue | 约 33.9 ms | 约 33.9 ms | - | - | 板端粗略关键路径 |

不要把 28.5 ms SOF-to-DQ 和 4.8 ms MPP 理解为完全串行的整帧处理。Sensor/CIF/ISP 存在按行 Pipeline 和多帧重叠；上表是观测边界，不是硬件内部每级都可独立分割。

## 10.3 Copy 与 DMA-BUF

| 路径 | 进程 CPU | 额外 CPU 整帧 copy | post-DQ mean |
| --- | ---: | ---: | ---: |
| copy + MPP | 8.2% | 1 次，3.11 MB，P50 2.03 ms | 6.872 ms |
| DMA-BUF + MPP | 3.2% | 0 | 5.230 ms |
| DMA-BUF + MPP + RTP | 5.0% | 0（但有压缩 packet 小 copy） | 含网络 sink |

CPU 从 8.2% 到 3.2% 是降低 5 个百分点，相对降幅约 61%。不要写成“降低 30%”，也不要声称已实测 DDR 带宽降低 25%；当前只有理论 DDR 流量估算，没有同轮计数器证据。

## 10.4 RGA 可选路径

| 实验 | 输入 | RGA 平均耗时 | CPU copy |
| --- | --- | ---: | ---: |
| file/copy input resize | 1920x1080 NV12 → 1280x720 | 约 2.721 ms | 实时 copy 实验还有约 1.25 ms 输入 copy |
| direct-MMAP resize | V4L2 MMAP → 1280x720 | 约 2.901 ms | 0 次输入整帧 copy |

Direct 消除 copy 不等于 RGA 本身每轮一定更快；调度、cache 和测量波动会影响同步 `imresize()` 时间。当前主链输出已是 1080p NV12，因此绕过 RGA 是最低开销方案。

## 10.5 启动耗时

| 阶段 | 实测量级 |
| --- | ---: |
| V4L2Capture init | 约 3 ms |
| MPP init | 5.08 ms |
| SPS/PPS header | 0.292 ms |
| `VIDIOC_STREAMON` 调用 | 140.9 ms |
| STREAMON 后到首次 DQBUF | 约 51 ms |

`STREAMON` 会向上游触发 Sensor global/mode/control 寄存器重写，所以比稳态单帧耗时大。启动耗时不能与每帧延迟混为一谈。

## 10.6 RTSP 长时间稳定性和端到端

| 指标 | 结果 |
| --- | --- |
| 长会话 | 21,561 帧 / 717.58 s / 30.05 fps |
| capture timeout/drop | 0 / 0 |
| 重连 | 两次连接/断开均可请求 IDR 恢复 |
| 同屏延迟样本 | 60 / 70 / 10 / 160 / 60 ms |
| 平均/中位/最大 | 72 / 60 / 160 ms |
| 时间趋势 | 120 s 内没有单调累积 |

这些样本使用屏幕计时器与 Camera 回显同框对比，受屏幕刷新、拍摄时刻和人工读数影响，适合证明数量级和无累积趋势，不是微秒级实验仪器结果。

## 10.7 RTP packet 正确性

120 帧抓包共 3205 个 RTP packet：

- kernel capture drop = 0。
- RTP sequence gap = 0。
- 120 个唯一 timestamp 组全部有 marker。
- 90 kHz timestamp 增量为 2999/3000/3001，平均 2999.99。

这证明一个图像帧虽被分成多个 RTP packet，但 timestamp 分组和帧尾 marker 正确。

## 10.8 3A 时序与资源

| 项目 | 结果 |
| --- | --- |
| 3A 开/关吞吐 | 均 300 帧、9.99 s、30.04 fps、0 timeout/drop/overrun |
| 固定输入时 3A 额外资源 | 约 0.9% CPU，16.1 MB RSS |
| 最新暗场 RKAIQ 观测 | 约 2.38% CPU，14.3 MB RSS（不同 workload） |
| 暗场 AE 参数 | 1 s 内 exposure 150→2995、gain 16→248、VBLANK 96→1449 |
| 暗场帧周期 | 约 60.339 ms |

暗场帧周期变长主要是 AE 为增加进光量而增大 VTS/曝光，不是 CPU 跑 AE/AWB 算法用了 60 ms。这是面试中很容易混淆的区别。

正常实景开启 3A 后的精确同屏毫秒延迟没有同轮证据，必须明确回答“当前无法从证据确认”，不能把 Stage 5 的 72 ms 直接当作 3A 实景数据。

## 10.9 测量代码怎么工作

`pipeline_stage_benchmark.cpp::process_frame()` 使用 `CLOCK_MONOTONIC` 在下列边界打点：

```text
wait_start
-> DQBUF returns
-> optional load_nv12 copy ends
-> MPP encode call starts
-> sink consume included in encode call
-> MPP encode call returns
-> QBUF returns
```

MPP 时间用 `encode_call_us - sink_push_us` 估算，避免把 GStreamer push 重复算入 MPP。每帧记录到 CSV，`SampleSeries` 再计算 mean/min/P50/P95/P99/max。

RKISP SOF 到 output done 使用 `/proc/rkisp1-vir0` 数据；用户态与 V4L2 timestamp 均为 monotonic 时域，才能计算 SOF-to-DQ。

## 10.10 实时约束和调度策略

- 30 fps 给出约 33.33 ms/帧的稳态节拍。
- DMA-BUF post-DQ 约 5.2 ms，明显小于帧周期，4 个 V4L2 buffer 有足够余量。
- 网络队列不允许无界积累，宁愿丢旧 packet 并请求 IDR。
- RKAIQ 在无 `CAP_SYS_NICE` 时回退 `SCHED_OTHER`，实测仍能保持 30.04 fps。
- 主链不需要 RGA 时必须绕过，否则凭空增加约 2.7-2.9 ms 及 DDR 读写。

## 10.11 面试中可以准确说的数据

> 在 1080p30 固定输入下，Sensor/RKISP 到用户态 DQBUF 的 P50 约 28.5 ms，MPP DMA-BUF H.264 编码 P50 约 4.83 ms，压缩 packet 进 GStreamer 的 P50 约 60.7 us。copy 对照路径每帧要额外拷贝 3.11 MB，P50 约 2.03 ms，进程 CPU 从 DMA-BUF 的 3.2% 升到 8.2%。板端 SOF 到 RTP 入队 P50 约 33.9 ms；PC 同屏端到端五组样本平均 72 ms、最大 160 ms，120 秒内无单调累积。

## 10.12 当前不能回答成实测值的项目

- Sensor 内部 ADC/readout、D-PHY、CSI 解包各自的独立耗时。
- RKISP 内部 BLC/demosaic/CCM/Gamma 每个 block 的独立微秒耗时。
- Wi-Fi 空口、Windows socket、jitter、D3D11 decode 的同轮统一时钟子段数据。
- 开 3A 的正常实景精确同屏延迟增量。
- 同轮 DDR 硬件计数器带宽和温度对照。

这些项目可以说“当前没有可分离的实测证据”，然后说明需要什么时间戳/硬件 trace 才能测。这比猫测更专业。
