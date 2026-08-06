# RK3588 摄像头链路任务交接文档

> 用途：在新建 Codex 对话时快速恢复任务上下文。最后更新：2026-08-06（Asia/Shanghai）。

## 0. 协作约束

这是学习项目。用户是主要实践者；Codex 负责原理讲解、拆分步骤、审查结果和
诊断。代码或板端修改前先说明目标、文件和风险。当前用户决定：先完成阶段 2
学习驱动的编写，再执行一次统一构建与实机验证；在此之前不得把代码改动描述为
已验证功能。

## 1. 当前状态

- 项目：Orange Pi 5 Pro（RK3588S）上的 OV13850 CAM2 摄像头链路。
- 项目阶段：阶段 2“V4L2 驱动完善”；阶段 0 基线与阶段 1 DTS 已有历史实机证据。
- 正式交付/参考驱动：`drivers/media/i2c/ov13850.c`。
- 当前学习实现：`drivers/media/i2c/ov13850_i2c_min.c`，仅能绑定
  `learning,ov13850-i2c`，不得与正式 `ovti,ov13850` binding 竞争。
- 2A 已写入但未验证的脚手架：V4L2 control 和 runtime-PM 头文件、control 状态
  字段、曝光/增益/VTS/测试图常量、link-frequency 与测试图菜单，并删除了
  `i2c:ovti,ov13850` alias。
- 尚未完成：controls 回调与初始化、runtime PM、两模式 TRY/ACTIVE 协商、完整
  probe/remove 清理、构建、模块装卸和实机测试。
- 当前分支：`main`；最近的阶段性本地提交为
  `5f9f87714 wip(ov13850): add stage 2 learning scaffolding`。继续前始终重新读取
  `git status -sb`，不要假定它已经推送。

## 2. 已验证的历史事实与当前边界

### 2.1 启动与网络基线

- 已验证内核 release：`6.1.99-opi5pro-livecfg-baseline`。
- 历史问题“串口停在 `Starting kernel ...`”的安全结论：只替换 Image 的基线可以
  启动；先前失败是多变量部署（Image、DTB、overlay、uInitrd、模块、启动配置）
  的组合问题，尚未定位到唯一 Kconfig 或 DT 根因。
- 新 release 缺少 Broadcom `bcmdhd.ko` 会导致 `wlan0` 不存在、SSH 失联；最小
  匹配模块树已是历史上验证过的恢复方式。

### 2.2 网络地址必须区分

- 历史 Wi-Fi 网络：`orangepi@192.168.1.17`，网关 `192.168.1.1`；SSID 未记录。
- 后来观测到的 Wi-Fi 网络：SSID `1702`，DHCP 地址
  `orangepi@192.168.0.112`。
- 两者都是历史记录，不代表板卡当前在线。任何 SSH、部署或摄像头结论前，先从
  串口或实际网络重新确认地址、`uname -r`、`wlan0` 和默认路由。
- 串口参数：`1500000 8N1`；历史 Windows 端口为 `COM20`。

### 2.3 OV13850 历史修复证据

正式驱动曾在 CAM2 冷启动时第一次读到 sensor ID `000000`。已验证的修复是在
`ov13850_probe()` 中，将这一次的 `-ENODEV` 清理资源后转换为 `-EPROBE_DEFER`。
随后重试探测到 `OV00d850`，media graph 闭合，且 `BG10 2112x1568` 单帧 RAW10
采集得到 4,415,488 字节文件。该证据说明当时的正式驱动、D-PHY、CSI/CIF 与 ISP
链路可工作；它不替代当前学习驱动的验证。

未连接的 `ov13855-2@36` 会报告 ID `000000`，是独立 DT 清理项，禁止与 OV13850
学习驱动改动混合部署。

## 3. 环境与路径

- Docker 实际工作树：`linux-orangepi-dev:/workspace/linux-orangepi`。
- WSL 发行版：`Ubuntu-22.04`；源码历史路径：`/home/wuage2335/linux-orangepi`。
- 交叉编译前缀：`aarch64-linux-gnu-`。
- 基线输出目录：`out/orangepi5pro-livecfg-baseline`。
- 学习模块输出目录：`out/orangepi5pro-2a-learning`。
- 所有内核构建必须使用独立 `O=`；独立输出构建 Image 时保留
  `KCFLAGS="-Wa,-I,$PWD"`。

## 4. 阶段 2 的设计与实施入口

按此顺序阅读：

1. `docs/codex/README.md`
2. `docs/superpowers/specs/2026-08-03-ov13850-stage2-learning-driver-design.md`
3. `docs/superpowers/plans/2026-08-03-ov13850-stage2-learning-driver-plan.md`
4. `drivers/media/i2c/ov13850_i2c_min.c`
5. `drivers/media/i2c/ov13850.c`（只读对照）

学习驱动目标是标准 V4L2 sensor sub-device：两个 RAW10 BGGR 模式
（2112x1568@30 与 4224x3136@7.5）、LINK_FREQ/PIXEL_RATE/HBLANK、VBLANK、
曝光、模拟增益与测试图 controls，以及由 `s_stream()` 持有引用的 runtime PM。

`v4l2_i2c_subdev_init()` 之后，I2C clientdata 是 `struct v4l2_subdev *`；必须
通过 `ov13850_min_from_client()` 和 `to_ov13850_min()` 回到私有结构，不能直接
转换成 `struct ov13850_min *`。

## 5. 阶段完成后的统一验证顺序

1. 在同一 `O=` 树先构建 `Image`，再构建 `ov13850_i2c_min.ko`，取得匹配的
   `vmlinux` 与 `Module.symvers`。
2. 检查 release、vermagic 和 `modinfo` aliases；不得存在
   `i2c:ovti,ov13850`。
3. 先在不改变真实 CAM2 binding 的条件下测试模块装卸。
4. 仅在有回滚路径的受控 DT 配置中测试 learning binding。
5. 执行两模式/controls 枚举、`v4l2-compliance`、连续 stream 启停、持续采集，
   并检查 D-PHY、CIF、ISP 和内核日志。

## 6. 文档维护

- 重要状态同时更新本文件、`task_plan.md` 与 `progress.md`。
- 构建/启动/部署问题追加到 `orangepi5pro-kernel-troubleshooting.md`。
- 只记录代码、构建输出、日志或实机状态支持的事实；未验证的实现必须明确标注。
- 提交前运行 `git diff --check`；推送前 `git fetch origin` 并确认没有远端分叉。
