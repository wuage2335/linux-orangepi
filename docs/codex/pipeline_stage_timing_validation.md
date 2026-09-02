# RK3588摄像头链路分阶段耗时实测

## 1. 测试目的

回答“相机链路每一项到底耗时多少”，并严格区分帧周期、模块同步调用耗时、
队列等待和光到屏端到端延迟。测试在Orange Pi 5 Pro实机执行，不用理论值替代。

## 2. 环境与数据量

```text
Board                 Orange Pi 5 Pro / RK3588S / 16GiB
Kernel                6.1.99-opi5pro-livecfg-baseline
Sensor input          2112x1568 SBGGR10
RKISP output          1920x1080 NV12, 3,110,400 bytes/frame
Encoder               MPP H.264 CBR 8Mbps GOP30
V4L2 buffers          4
CPU governor          ondemand
Fixed workload        test pattern 1, exposure1000/gain16/VBLANK96
Samples               5 runs x 300 frames = 1500 frames/group
Warmup                30 frames/run
Statistics            nearest-rank P50/P95/P99
```

原始板端目录：
`/home/orangepi/pipeline-timing-results-20260902_122528`。最终下载包SHA256：
`c9875c14dbac47cd9b8f8eddbaf2e76db7ba9babf9b78a7391d26196d9e0d1a9`。

## 3. 指标定义

| 指标 | 起点 | 终点 | 边界 |
| --- | --- | --- | --- |
| frame interval | 前一帧V4L2 timestamp | 当前帧timestamp | 帧周期，不是处理耗时 |
| ISP output delay | RKISP记录的SOF | mainpath buffer done | 包含sensor读出重叠、ISP和DMA |
| SOF to DQBUF | V4L2 timestamp | 用户态DQBUF返回 | ISP输出加vb2唤醒/调度 |
| copy | `load_nv12()`前 | `load_nv12()`后 | 3.11MB CPU copy和padding |
| RGA | `imresize()`前 | 同步返回 | 1920x1080到1280x720 |
| MPP | encode调用前 | packet返回 | frame提交、硬件编码、packet取回 |
| GStreamer push | sink `consume()`入口 | appsrc push返回 | 压缩packet复制和入队，不含网络到达 |
| post-DQ | DQBUF返回 | MPP完成并QBUF | 用户态持有采集buffer的关键区 |

RKISP内核数据来自`/proc/rkisp1-vir0`；用户态使用`CLOCK_MONOTONIC`，与
`V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`对应。

## 4. 固定30fps主链路结果

### 4.1 关键结果

| 项目 | Mean | P50 | P95 | P99 |
| --- | ---: | ---: | ---: | ---: |
| 帧周期 | 33.280ms | 33.280ms | 33.306ms | 33.354ms |
| RKISP SOF到mainpath完成 | 27ms | 27ms | 27ms | 27ms |
| SOF到用户DQBUF | 28.482ms | 28.501ms | 28.602ms | 28.627ms |
| 3.11MB CPU copy | 1.936ms | 2.030ms | 2.089ms | 3.086ms |
| MPP DMA-BUF编码 | 4.780ms | 4.832ms | 4.962ms | 5.022ms |
| MPP copy输入编码 | 4.551ms | 4.581ms | 4.688ms | 4.877ms |
| DMA-BUF QBUF | 0.448ms | 0.559ms | 0.607ms | 0.643ms |
| DMA-BUF DQ到重新QBUF | 5.230ms | 5.358ms | 5.522ms | 5.587ms |
| copy DQ到重新QBUF | 6.872ms | 7.020ms | 7.132ms | 8.566ms |

`SOF到DQBUF - RKISP output delay`约为1.5ms，但内核proc只有整数毫秒精度，
因此只把它视为vb2完成、唤醒和用户态调度的近似值。

### 4.2 DMA-BUF收益

copy路径增加约1.94ms/帧CPU复制。虽然DMA-BUF的单次MPP调用在本轮比copy模式
高约0.23ms，完整post-DQ关键区仍从平均6.87ms降到5.23ms，节省约1.64ms。

进程资源对比：

| 路径 | CPU | Max RSS |
| --- | ---: | ---: |
| copy + MPP | 8.2% | 20,604KB |
| DMA-BUF + MPP | 3.2% | 20,604KB |
| DMA-BUF + MPP + RTP | 5.0% | 27,911KB |

因此DMA-BUF的主要收益是移除大帧CPU复制和降低CPU，不代表设备不读写DDR。

## 5. GStreamer/RTP耗时

固定彩条的压缩数据很小，GStreamer push平均48.73us、P95 93.04us。为避免静态
彩条低估成本，又在当前实景、约8Mbps输出下补测1500帧：

| 项目 | Mean | P50 | P95 | P99 |
| --- | ---: | ---: | ---: | ---: |
| GStreamer packet copy/appsrc push | 66.45us | 60.67us | 89.83us | 189.87us |
| MPP（扣除sink push） | 4.206ms | 4.203ms | 4.250ms | 4.383ms |
| DQ到RTP入队完成 | 4.554ms | 4.546ms | 4.655ms | 4.735ms |

该结果只到`gst_app_src_push_buffer()`返回。UDP进入内核、Wi-Fi传输、Windows
jitter buffer、解码和Present不在这66us中。

## 6. RGA耗时

五轮RGA每轮300帧；RGA使用官方librga 1.10.6_[3]：

| 路径 | 五轮平均 | 各轮均值范围 | 各轮P95范围 |
| --- | ---: | ---: | ---: |
| copy阶段 | 1.253ms | 1.161-1.322ms | 1.329-1.338ms |
| copy输入RGA | 2.721ms | 2.633-2.784ms | 2.797-2.817ms |
| direct-MMAP RGA | 2.901ms | 2.518-3.153ms | 2.575-3.254ms |

Direct确定移除了copy并降低RSS/CPU，但在`ondemand`调频下RGA同步耗时波动更大，
不能声称direct每轮都比copy的`imresize()`本身更快。当前1080p编码无需resize，
生产主链路应直接绕过RGA。

## 7. 启动耗时

| 项目 | 五轮平均 | 范围 |
| --- | ---: | ---: |
| V4L2 capture初始化（DMA导入4 buffers） | 3.00ms | 2.94-3.08ms |
| MPP encoder初始化 | 5.08ms | 4.16-6.97ms |
| MPP SPS/PPS header | 0.292ms | 0.252-0.350ms |
| QBUF全部buffer并执行STREAMON | 140.9ms | 125.4-150.3ms |
| STREAMON返回后等待首个DQBUF | 51.0ms | 50.7-51.2ms |

因此从调用`capture.start()`到拿到首帧约192ms。GStreamer RTP sink首次初始化
为15ms到1.34s，最大值来自首次插件registry/加载冷启动；后续实景热启动约
16.6-48.8ms，不应把首次冷启动成本算进每帧延迟。

## 8. 3A当前实景交叉验证

当前场景较暗，RKAIQ收敛到：

```text
exposure=2995
analogue_gain=198
VBLANK=1449
```

结果：

| 项目 | 固定30fps | 3A暗场 |
| --- | ---: | ---: |
| 帧周期 | 33.280ms | 60.339ms（16.57fps） |
| RKISP output delay | 27ms | 29ms |
| SOF到DQBUF P50 | 28.501ms | 29.782ms |
| MPP P50 | 4.832ms | 4.206ms |

60.339ms主要来自AE主动增大VTS/曝光，不是3A算法在CPU上处理了60ms。RKAIQ
进程CPU样本均值2.38%，RSS均值14,341KB；它与图像硬件流水并行运行。

## 9. 流水线关键路径解释

固定DMA-BUF+RTP的典型P50：

```text
SOF -> DQBUF               28.494ms
DQBUF -> MPP/GStreamer/QBUF 5.398ms
SOF -> 板端RTP入队          约33.892ms
```

这些模块是流水运行的，不能把每帧33.28ms等待、27ms ISP和4.8ms MPP全部简单
相加。下一帧Sensor曝光时，上一帧可以正在ISP/MPP中处理。

加入推荐30ms jitter buffer后，板端约33.9ms加接收缓存约30ms，已经接近此前
同屏典型72ms；余量来自Wi-Fi、解码和D3D11 Present。历史RTSP同屏五组为
60/70/10/160/60ms，平均72ms。该历史同屏法仍是光到屏总延迟证据。

## 10. 稳定性与边界

- 所有固定矩阵15轮、RGA 10轮、3A 10轮均0 timeout、0 sequence drop、
  0 queue overrun。
- 测试后sensor PM均为`suspended/0`。
- 新增日志无RKISP/MPP/RGA/MMU/IOMMU fault。
- 温度从44.384C升到47.153C。
- `output delay=27ms`是SOF到mainpath buffer完成，不是单独某个ISP硬件模块的
  纯算术耗时；若要继续拆分demosaic/NR/CCM，必须增加内核或硬件trace点。
- 板端与Windows没有亚毫秒级同步时钟，本次不伪造独立Wi-Fi单程耗时。

## 11. 面试回答建议

> 在1080p30固定场景下，我用V4L2单调时间戳、RKISP procfs和用户态逐帧打点做了
> 5轮、每轮300帧测试。SOF到ISP mainpath完成是27ms，用户态拿到buffer约28.5ms；
> 3.11MB CPU copy P50约2.03ms，DMA-BUF把它降为0；MPP H.264同步编码P50约
> 4.83ms；真实8Mbps码流推入GStreamer P50约0.061ms。因此SOF到板端RTP入队
> P50约33.9ms。接收端30ms jitter后，历史光到屏典型约72ms。各阶段流水并行，
> 不能把33ms帧周期、27ms ISP和5ms编码机械相加。
