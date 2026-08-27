# Stage 5 Shared RTSP、重连与实时PTS验证

## 1. 范围

验证链路：

```text
OV13850 -> RKISP 1920x1080 NV12@30
-> V4L2 DMA-BUF -> MPP H.264
-> shared GStreamer RTSP server
-> Windows GStreamer/VLC
```

板端为Orange Pi 5 Pro、内核`6.1.99-opi5pro-livecfg-baseline`、GStreamer
1.20.1、官方MPP 1.1.0。基线参数为CBR 8Mbps、GOP30、B=0、queue2、MTU1200。

## 2. 实现

- `GstRtspServerSink`创建shared media factory；
- 始终只有一套V4L2 capture和MPP encoder；
- 无客户端时继续消费编码输出但不积压网络包；
- 新客户端到来时补发SPS/PPS并请求IDR；
- `appsrc`引用由media configure/unprepare管理，并用mutex隔离GLib和编码线程；
- bus error转为进程失败，退出时STREAMOFF、join线程并释放对象。

自动化测试启动服务，先后连接两个GStreamer客户端，每次至少解码30帧，要求同一
服务进程继续存活，并检查`RTSP_CLIENT_CONNECTED`、`IDR_REQUESTED`和
`RTSP_CLIENT_DISCONNECTED`。

## 3. 初版长期失稳与根因

初版首次连接和重连都成功，但Windows软/硬解码在40-60秒后固定出现花屏、卡顿和
延迟累积。UDP更早花屏，TCP表现为旧帧积压；重连暂时恢复。

排查证据：

```text
old server frames=40627
old server elapsed=1352.09s
actual cadence=30.05fps
V4L2 timeout/drop=0/0
queue full/leak/overrun=0/0/0
Wi-Fi=5GHz, -41dBm, tx about 390Mbps
```

旧代码以`frame_index / 30`生成PTS。30.05fps实际采集会让媒体时间每分钟比真实
时间超前约95ms，超过30ms jitter窗口后接收端开始积压和重排。queue、decoder和
摄像头均不是根因。

## 4. 实时PTS修复

新增`LivePtsClock`，以`gst_util_get_timestamp()`的单调时间建立每个media的
零基时间轴。media重建时reset；异常时钟回退不能让PTS倒退。纯C++测试覆盖：

1. 33,280us真实帧间隔不会被改写为33,333us；
2. reset后新media从0开始；
3. 回退采样不会使PTS倒退。

RTP sender既有90kHz固定时间戳路径没有修改。

## 5. 重连验证

板端自动化两轮结果：

```text
decoded_frames=176
decoded_frames=176
connections=2 disconnects=2 idr_requests=2
PASS: shared RTSP reconnect recovery
```

用户实测GStreamer连续两分钟无花屏、卡顿或延迟累计，关闭后再次连接正常。VLC
首次播放和重连也正常，但默认端到端延迟约400ms；激进时钟参数约600ms，因此
VLC只作为兼容性客户端。

## 6. 五组端到端延迟

同屏秒表法：截图同时包含PC原始计时器和摄像头回传计时器，二者差值为整条链路的
近似延迟。

| 采样点 | 原始/回传差值 |
| ---: | ---: |
| 10s | 60ms |
| 30s | 70ms |
| 60s | 10ms |
| 90s | 160ms |
| 120s | 60ms |

```text
mean=72ms
median=60ms
min=10ms
max=160ms
```

五组均低于200ms，且无单调增加。截图和SHA-256位于
`docs/stage_5_photo_record/task9/`。

## 7. Stage 4/5总回归

2026-08-28实机结果：

| 项目 | 结果 |
| --- | --- |
| 官方MPP | bundle SHA通过；官方H.264编码30帧 |
| 文件编码 | H.264 300帧264.58fps；H.264 CBR/VBR、H.265 CBR通过 |
| 官方decoder | H.264/H.265、实时copy/DMA-BUF均解到预期帧数 |
| 实时copy | 300帧，30.03fps，copy 1774.06us，CPU 7% |
| 实时DMA-BUF | 300帧，30.03fps，copy 0us，CPU 6% |
| RTP | 300帧，30.03fps，0 timeout/drop/overrun |
| RTSP | 两次客户端各解码176帧，2连接/2断开/2 IDR |
| PM | 退出后suspended，usage 0 |
| fault | 无新增CSI/ISP/MPP/RKVENC/MMU/IOMMU fault |

板端原始日志位于
`~/ov13850_opi5pro_learning/stage5/task9-rtsp/measurements/`，大码流不纳入Git。

## 8. 结论边界

阶段5的PC低延迟播放、RTP timing、shared RTSP、重连、实时PTS和总回归完成。
手机播放由用户明确移为非重点可选项，不作为关闭条件。画面偏暗偏绿仍属于独立
RKAIQ 3A/IQ工作项；网络扰动和更长时间压力测试进入阶段6。
