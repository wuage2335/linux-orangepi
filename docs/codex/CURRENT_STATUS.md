# Camera 项目当前状态（2026-09-27）

本页统一描述最新状态；带日期的设计、计划和验收报告保留当时条件。历史数字不能
替代当前场景结果，CPU内存基准不能替代全SoC DDR总线流量。

## 工作位置与运行基线

- 主仓库：WSL `Ubuntu-22.04:/home/wuage2335/linux-orangepi`，分支`main`。
- 远端：`https://github.com/wuage2335/linux-orangepi`。
- Windows `Camera开发` 是文档/证据中转目录，其外层Git属于另一个项目。
- Docker `linux-orangepi-dev:/workspace/linux-orangepi` 为历史实施环境，使用前核实。
- 板卡：Orange Pi 5 Pro / RK3588S、16GiB、OV13850 CAM2。
- 最近验证地址：`orangepi@192.168.1.16`，DHCP可能变化；Windows SSH已有可用密钥。
- 内核：`6.1.99-opi5pro-livecfg-baseline`；Image SHA256
  `e5312723b9192fdb59fcf60b6770490e149888f8ec44d002cbde0ee5699d0f19`。
- 学习驱动：`ov13850_i2c_min.c`，绑定`learning,ov13850-i2c`，历史实机基线为内建。
  正式`ov13850.c`保留作参考，两者不能争抢binding。
- 最近验证节点：视频`/dev/video11`、sensor`/dev/v4l-subdev2`；运行前重新确认。
- 板端收口部署：`~/ov13850_opi5pro_learning/release/closeout-20260915/`。

## 已完成

阶段0–6已验收：驱动/DTS、RKISP、RGA实验、MPP编码、RTP/RTSP、RKAIQ/3A、性能
与稳定性。阶段7功能扩展尚未实施。

- 2026-09-18合并到`main`的改造完成MPP packet到GStreamer
  的payload零拷贝：共享owner保持MPP输出buffer，GstBuffer包装同一地址，appsrc
  与下游queue均有界。板端300帧DMA-BUF RTP为30.04fps、0 timeout/drop/overrun，
  RTSP两次连接解码与IDR恢复通过；证据位于板端`zero-copy-20260918/evidence/`。
- 随后增加固定MPP packet buffer池，默认2个。板端1–4 buffer扫描及慢客户端
  压力中实际峰值始终为1，1-buffer也保持30.04fps、RTSP重连解码和0 pool miss，
  因而实测最小值为1；默认2用于保留安全余量。固定池CPU与动态池同为约4–5%，
  主要收益是DMA输出内存有严格上限。证据位于`two-buffer-20260918/evidence/`。

- 主链：OV13850 → RKISP → V4L2 NV12 → DMA-BUF → MPP H.264 → RTP/RTSP。
- 双模式、Controls、runtime PM、TRY/ACTIVE、生命周期与重复启停已有实机证据。
- MPP支持H.264/H.265文件编码；当前RTSP业务只支持H.264。
- RGA resize有独立copy/direct-MMAP验证，尚未集成RGA输出DMA-BUF→MPP主链。
- WSL host与板端aarch64完整构建恢复，SDK架构分开；新增12项脚本测试通过。
- 一键构建、会话、回归、打包和资源摘要脚本已交付；运行库使用原子替换，构建
  与会话共用锁。源码中文注释/Doxygen及真实Doxygen解析已完成。
- DFI时钟overlay已由用户部署并重启，dmc注册恢复；备份位于
  `/boot/camera-closeout-backup-20260915-234106`。

## 最近实测与口径

| 实验 | 结果 | 证据 |
| --- | --- | --- |
| 最新固定测试图、pool2，5×300帧 | 30.05fps；post-DQ P50/P95 4.497/4.680ms；CPU 3%；peak1/miss0 | [阶段耗时](pipeline_stage_timing_validation.md) |
| 最新固定曝光实景、pool2，5×300帧 | push P50/P95 23.916/46.958us；MPP+sink 4.368/4.453ms；CPU 4% | 同上 |
| 最新RTP 1800帧 | 30.05fps，0 timeout/drop/overrun/miss，60 IDR，RSS峰值28,292KB | 同上 |
| 最新RTSP重连 | 客户端解码145/178帧，连接/断开/IDR各2次，退出PM通过 | 同上 |
| 固定曝光600秒 | 18011帧，30.05fps，0采集timeout/drop | [工程收口](engineering_closeout_validation.md) |
| 3A实景1800秒 | 44591帧，24.76fps，0采集timeout/drop，重连及PM退出通过 | 同上 |
| 3A资源 | server/RKAIQ平均CPU 3.344%/3.689%，SoC峰值60.076°C | 同上，单核100%口径 |
| DFI修复后180秒 | 4461帧，24.76fps，0采集timeout/drop；Windows D3D11解码90帧 | 同上 |
| DMC监测 | 工作2.4GHz、空闲534MHz；3A阶段load均值3.77% | 最忙通道利用率，非MB/s |
| CPU有效带宽，大核64MiB | 读18.371、写27.271、复制12.540GB/s | [DDR基准](ddr_cpu_bandwidth_validation.md) |
| CPU有效带宽，四大核 | 读22.802、写26.009、复制10.558GB/s | 同上；三轮中位数 |

3A会随照度延长曝光/VTS，不能保证所有实景30fps。2026-08-28的五组同屏延迟
60/70/10/160/60ms（均值72ms）仍是历史Stage5条件下的样本，不能当作当前3A
显示延迟或P95。各轮测试的温度、照度、帧率和客户端负载必须和数字一起引用。
最新复测未运行RKAIQ，也未重测光到屏延迟、RGA或DDR物理带宽。当前STREAMON
均值约1.14秒，高于历史140.9ms，尚未定位唯一根因，不能归因给固定packet池。

## 未完成与当前选择

1. **CPU0 TASKLET异常待定位。** 2026-09-16观察到ksoftirqd/0约97% CPU，3秒
   TASKLET增加19,529,492次。内存基准避开CPU0，但不能称为无干扰峰值；尚未
   证实具体驱动或与DFI的因果关系。
2. **精确3A同屏显示延迟暂缓。** 用户明确选择先不做，后续需同屏可读计时样本。
3. **全通道DDR物理读写MB/s未测。** DMC利用率与CPU payload带宽已测，二者不能
   替代物理总线计数；普通缓存内存成绩也不能代替DMA-BUF映射性能。
4. V4L2合规历史为42/43，遗留control event订阅；未接OV13855节点仍是独立清理项。
5. 含原生module-info的候选Image历史上未部署；DFI修复只改overlay，不代表该
   候选内核已经安装。活动3A继续使用私有runtime-v15兼容包。
6. AI事件相机、WebRTC平台、ROS2/STM32、Rust服务和语义检索仅讨论过方向，尚未
   选定实施方案；见[扩展备选](extension_options.md)。

## 一键运行入口

从板端`closeout-20260915/ov13850_opi5pro_learning/`执行：

```bash
python3 streaming/scripts/camera_session.py --output ./evidence/live-new
```

输出目录必须是新的；默认启动已有runtime-v15、配置ISP并启动RTSP。Ctrl+C退出。
固定时长回归可加`--duration 180 --decode-check`，不要在活动release运行时构建。

Windows从源码的学习工程目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\streaming\scripts\receive_h264_rtsp.ps1 -Uri rtsp://192.168.1.16:8554/live -LatencyMs 30 -Decoder auto
```

工具职责与编译方式见[streaming README](../../ov13850_opi5pro_learning/streaming/README.md)。
DDR复现工具见[benchmarks/ddr](../../ov13850_opi5pro_learning/benchmarks/ddr/README.md)。

## 证据保存

- 板端`release/closeout-20260915/`：完整矩阵、fixed-600s、soak-3a-1800s-final、
  post-dfi-reboot及原始失败/被替代运行。
- 板端`release/ddr-20260916/`：读写基准、固定版tinymembench及原始采样。
- Windows`engineering-closeout-work/artifacts/`和`ddr-test-work/results/`：证据副本。
- 不把中断/失败组标记通过，不覆盖原始日志，不用历史结论替代新配置实测。
