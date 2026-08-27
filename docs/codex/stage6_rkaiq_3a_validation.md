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
  单摄 online/no-readback，以及当前 ISP3 params/stats 的两个 ABI 字段。
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

## 5. 板端结果

### 5.1 已通过

```text
module-info: sensor=ov13850 module=CMK-CT0116 lens=default
no-stream server: 连续 15 秒等待事件，timeout 返回 124，无崩溃
online capture: 90 帧，30.05 fps，输出 279936000 bytes
12 秒持续流: 360 帧，30.05 fps
MPP/RTP 回归: 300 帧，30.04 fps，0 timeout/drop/overrun，10 IDR
service stop: sensor PM suspended, runtime_usage=0
```

### 5.2 尚未通过

动态 AE/AWB 未闭环。v12 中 `/dev/video18` 能以 16172 字节 buffer 启动，但
`isp_3a_stats_poll` 持续超时，没有 dequeue；曝光保持初始化值 150 行，增益保持
16。2022 同版本自带的 OV13855 ISP3x IQ A/B 也没有动态 stats，排除仅由当前
OV13850 JSON 转换造成。

当前黑暗场景的单帧统计：

```text
3A 前历史基线: Y mean 4.030-4.049, U/V mean 128
v12 初始化参数: Y mean 0.005, U mean 128.000, V mean 128.000
手动曝光瞬态:   Y mean 0.937, U mean 128.766, V mean 128.011
```

因此不能宣称暗绿问题已经修复，也不能把静态 AWB 初值当作自动白平衡证据。

## 6. 当前安全状态与下一步

板端最终已停止 RKAIQ，恢复 `exposure=1536`、`analogue_gain=16`，PM 为
`suspended/0`。下一步应定位当前 6.1 `rkisp-statistics` 不出队的条件，优先比较
同内核版本配套 RKAIQ userspace 或追踪 stats/params stream 状态；完成动态 stats
前不要安装 systemd 服务，也不要把私有 bundle 覆盖到系统库。

永久内核候选已在独立输出目录构建，但未部署：

```text
Image: 41396736 bytes
Image SHA256: cf75d6d2a0f40f455c123d8a1067aab607b14ed26be81ce44344bd68e9f63e61
vmlinux SHA256: 51fdb4271125d64dab7d7364d10f4f9c531cc90f103dc634dfc0e628f20c3096
symbols: ov13850_min_ioctl, ov13850_min_compat_ioctl32
```
