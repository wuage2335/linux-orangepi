# Stage 6 RKAIQ/3A 接入验证记录

## 1. 目标与边界

目标是为 Orange Pi 5 Pro 的 OV13850 CAM2 接入 RKAIQ，使 RKISP 获得 AE、AWB、
CCM 和 Gamma 等参数。所有实验均使用用户目录中的私有 bundle 和 IQ 副本；未安装
系统服务、未覆盖 `/etc/iqfiles`、未替换活动内核，也未无人值守重启。

## 2. 固定环境

```text
Board: Orange Pi 5 Pro / RK3588S / 16 GiB
Kernel: 6.1.99-opi5pro-livecfg-baseline
Sensor: OV13850 CAM2, /dev/v4l-subdev2, I2C 3-0010
Mainpath: /dev/video11, 1920x1080 NV12, 30.05 fps
IQ source: /etc/iqfiles/ov13850_CMK-CT0116_default.json
RKAIQ upstream: camera_engine_rkaiq rk3588
Commit: 5af997da2442a504b1005cb804a75745171dc522
Version: AIQ v3.0x9.1 / ISP_HW_V30
```

## 3. 已完成实现

- 学习驱动实现 `RKMODULE_GET_MODULE_INFO` 和 compat ioctl，并读取 Rockchip DT
  module index、facing、module、lens 属性。
- 私有构建固定上游提交，只构建 `librkaiq.so` 与 `rkaiq_3A_server`。
- LD_PRELOAD shim 仅在 `RKAIQ_MODULE_INFO_SHIM=1` 时补旧活动内核缺失的 module
  ioctl；兼容 AArch64 ioctl 命令的 32 位符号扩展。
- 兼容补丁处理实体名、2112x1568、`/dev/video11`、IQ 私有路径、固定焦点 AF、
  单摄 online/no-readback、ISP3 params/stats ABI，以及实时线程权限不足时的
  `SCHED_OTHER` 回退。
- IQ 转换器只写运行目录副本，映射旧解析器需要的模块键与 AE/AWB 枚举；系统 IQ
  SHA 保持 `949c98a7ab6a60ecb86190406fbcae797742afb428bdc31a98850940ccaf6999`。

## 4. 故障定位结果

1. 初始 module-info ioctl 因旧活动内核返回 `ENOTTY`，shim 后探针得到
   `ov13850 / CMK-CT0116 / default`。
2. 新 IQ 键 `adrc_calib_v11` 被旧解析器忽略，导致 `Calib2stAutoV30()` 空指针；
   映射到 `adrc_calib_V2` 后不再崩溃。
3. 模组无 VCM，但旧 AF handler 固定请求连续 AF，prepare 失败；无 focus support
   时禁用 AF 后服务进入 stream-event 循环。
4. `setMulCamConc(..., 1)` 和旧 multiplex 探测把单摄链路误切到 readback；显式
   online 后 `/dev/video11` 恢复 30.05 fps。
5. 当前内核 `rkisp3x_isp_stat_buffer` 比旧头多 `params_id`，params 多
   `exposure`；最小 ABI 补丁可编译并匹配 16172 字节 stats buffer。
6. 内核 debug=4 证明 seq 0-3 stats 已完成，随后 buffer 未被用户态回收。根因是
   普通用户无 `CAP_SYS_NICE`，`SCHED_RR` stats poll 线程创建失败且返回值被忽略；
   v13 回退 `SCHED_OTHER` 后，普通用户下 AE/AWB 每帧运行。

## 5. 板端结果

### 5.1 已通过

```text
module-info: sensor=ov13850 module=CMK-CT0116 lens=default
no-stream server: 连续 15 秒等待事件，timeout 返回 124，无崩溃
online capture: 90 帧，30.05 fps，输出 279936000 bytes
12 秒持续流: 360 帧，30.05 fps
MPP/RTP 回归: 300 帧，30.04 fps，0 timeout/drop/overrun，10 IDR
v13 非 root AE: 1 秒内 exposure 150->2995、gain 16->248、VBLANK 96->1449
v13 AWB: 连续约 90 帧执行，暗场 gain=(1.7498,1.0,1.0,1.6254)
test pattern 1: Y mean 128.132，exposure8/gain16/VBLANK96，30.05 fps
test pattern 2: Y mean 194.793，exposure8/gain16/VBLANK96
test pattern 3: Y mean 128.815，exposure8/gain16/VBLANK96
test pattern 4: Y mean 0，exposure2995/gain248/VBLANK1449
service stop: sensor PM suspended, runtime_usage=0
```

Type 1 NV12 转换后的彩条顺序为白、黄、青、绿、品红、红、蓝、黑，目视无全局
偏绿或 U/V 交换。证据图片为
[`assets/stage6_ov13850_pattern1.png`](assets/stage6_ov13850_pattern1.png)，SHA256
为 `b383c90dcc6bc1ca9774f072746271c7420981231d34669dab7bc39e29461813`。

### 5.2 历史人工验收缺口（已解决）

无人值守阶段无法确认显示器是否真正照亮摄像头，因此当时的白屏A/B不能作为
亮场证据。用户随后完成真实bright/normal/dark采集和PNG目视检查，结果见5.4；
该历史缺口已经解决。

### 5.3 3A 开关性能对比

固定 Type 1 pattern、同一 300 帧 DMA-BUF/MPP/RTP 参数：

| 项目 | 3A 开 | 3A 关 |
| --- | --- | --- |
| 内部 elapsed/FPS | 9.99s / 30.04 | 9.99s / 30.04 |
| timeout/drop/overrun | 0/0/0 | 0/0/0 |
| sender CPU | 3% | 2% |
| sender max RSS | 27732KB | 27728KB |
| 3A 进程 | 约0.9% CPU，16100KB RSS | 无 |

当前测量分辨率内没有帧周期或队列积压增量；额外资源约为一个 0.9% CPU、16MB RSS
的 3A 进程。精确端到端毫秒差仍需用户把摄像头对准同屏计时器后复测。

### 5.4 真实三场景验收

用户于 2026-08-28 10:12 完成真实 bright/normal/dark 采集：

| 场景 | exposure | gain | VBLANK | Y mean | U mean | V mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| bright | 1498 | 21 | 96 | 123.808 | 128.704 | 127.992 |
| normal | 1997 | 65 | 451 | 121.255 | 127.872 | 128.408 |
| dark | 2995 | 248 | 1449 | 79.220 | 127.677 | 131.985 |

AE 随照度降低逐级增加曝光、增益和 VTS；bright/normal 的目标亮度稳定在约 122，
dark 达到 sensor 上限后仍保持 Y mean 79.220。三帧转为 PNG 后目视检查：bright
和 normal 为中性灰白，无全局偏绿；dark 在最大增益下有轻微暖紫噪声，但不再
出现原始绿色覆盖。

原始汇总证据为
[`assets/stage6_real_scenes_summary.tsv`](assets/stage6_real_scenes_summary.tsv)，
SHA256 为 `ed9552e60edee677f74358d0e07d33cf4da12f5f06f7621df0a39d9e35977a9c`。

### 5.5 性能、稳定性与延迟影响收口

- Stage 5修复实时PTS后，RTSP同屏五组延迟为60/70/10/160/60ms，平均72ms、
  最大160ms，120秒内无单调增长；长会话21,561帧、717.58秒、30.05fps、
  0 timeout/drop。
- 固定Type1输入时，3A开/关的MPP/RTP均为300帧、9.99秒、30.04fps、
  0 timeout/drop/overrun。3A新增约0.9% CPU和16MB RSS，没有增加帧周期或队列
  积压。
- 正常实景`summary.tsv`中的`latency_ms`为`unknown`，因此没有证据给出3A开启后
  的精确同屏毫秒值。收口只得出“未观察到吞吐和队列回归”，不把Stage 5的
  72ms平均值冒充3A实景测量值。
- DDR带宽和温度也没有形成与3A开/关同一轮、可复现的实测，作为可选补测保留。

当前主要开销边界：接收端30ms jitter和显示刷新属于端到端固定组成；copy路径
约1.82ms/帧，DMA-BUF已移除；需要resize时RGA约1.63-3.17ms，不需要变换时应
绕过；3A不是当前帧率或队列瓶颈。

## 6. 收口状态与安全边界

板端最终已停止 RKAIQ，恢复 `exposure=1536`、`analogue_gain=16`，PM 为
`suspended/0`，`video_rkisp.debug` 已恢复 0。真实三场景的 AE/AWB 与画质已通过；
Stage 6于2026-08-30完成。精确3A实景同屏延迟、DDR带宽和温度属于已声明的
可选补测，不阻塞阶段关闭。正式部署前仍不安装systemd服务或覆盖系统库。

板端已部署带完整现场验收入口的 `runtime-v15`，bundle SHA256 为
`0ef8f0a32da4516563160656e114ed5cc7f6ed06ee6d5a28fee259dd3a0e6b55`：

```bash
V15=~/ov13850_opi5pro_learning/stage6/rkaiq-3a/runtime-v15

"$V15/bin/validate_real_scenes.sh" \
  --bundle "$V15" \
  --configure ~/ov13850_opi5pro_learning/stage5/task7-live-rtp/configure_rkisp_1080p.sh
```

脚本依次提示 bright/normal/dark，自动等待收敛、抓取 NV12、统计 Y/U/V、记录
controls、SHA256 和人工输入的同屏延迟；退出时停止 RKAIQ 并恢复稳定 controls。

永久内核候选已在独立输出目录构建，但未部署：

```text
Image: 41396736 bytes
Image SHA256: cf75d6d2a0f40f455c123d8a1067aab607b14ed26be81ce44344bd68e9f63e61
vmlinux SHA256: 51fdb4271125d64dab7d7364d10f4f9c531cc90f103dc634dfc0e628f20c3096
symbols: ov13850_min_ioctl, ov13850_min_compat_ioctl32
```
