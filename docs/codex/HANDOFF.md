# RK3588 摄像头链路任务交接文档

> 用途：在新建 Codex 对话时快速恢复任务上下文。最后更新：2026-07-20（Asia/Shanghai）。

## 0. 最高优先级协作约束

这是用户的学习项目。Codex 应以导师和结对伙伴的方式推进，不得默认一次性代替用户完成整个阶段或完整功能。

默认协作方式：

1. 先解释当前知识点、目标和判断依据。
2. 将任务拆成用户可以理解和执行的小步骤。
3. 用户编写代码或执行关键实机命令，Codex 负责审查、诊断和下一步指导。
4. 用户明确授权直接检查或修复时，Codex 可以主动访问 WSL、SSH 和串口，但涉及板端 `sudo` 密码时只提供命令，不索要密码。
5. 任何阶段都必须以代码、构建结果和实机证据验收，不能仅因代码已经生成而标记完成。

## 1. 当前状态

- 当前阶段：阶段 2“V4L2 驱动完善”。阶段 0“基线验证”和阶段 1“传感器与 DTS”此前已经完成。
- 当前板卡：Orange Pi 5 Pro，RK3588S。
- 最近一次实机验证（2026-07-18）：板端运行 `6.1.99-opi5pro-livecfg-baseline`，`bcmdhd` 已自动加载，`wlan0` 为 `192.168.1.17/24`，SSH 正常。
- 2026-07-20 文档同步时 SSH 连接超时；板卡可能未上电或不在原网络，当前实时状态尚未复核。新对话应先确认板卡在线，再沿用最近验证结果。
- SSH：`orangepi@192.168.1.17`。
- 串口：Windows `COM20`，波特率 `1500000`。串口工具占用 COM20 时不要强行打开第二个会话。
- 当前摄像头链路尚未完成绑定。启动日志仍有 `update sensor info failed -19` 和 `get remote terminal sensor failed`。
- 下一步只处理 OV13850 驱动与设备树的 `compatible`、endpoint 和 media graph 绑定，不修改已经验证的内核、Wi-Fi 和启动基线。

## 2. 项目目标

围绕岗位所需的“RK3588 摄像头链路 + V4L2 驱动 + ISP/MPP/RGA + 低延迟视频流”能力，完成一套可运行、可测量、可演示的项目。

目标链路：

`OV13850/MIPI -> CSI/DPHY -> CIF/ISP -> V4L2 -> RGA（按需）-> MPP -> RTP/RTSP -> PC 播放`

第一版目标：1080p30、NV12、H.264、RTP/RTSP，重点验证低拷贝、低延迟、稳定运行和故障可定位。

## 3. 环境与路径

- WSL 发行版：`Ubuntu-22.04`。
- 从 PowerShell 进入 WSL 的正确命令：`wsl -d Ubuntu-22.04`。
- Linux 内核源码：`/home/wuage2335/linux-orangepi`。
- Windows UNC 路径：`\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi`。
- Orange Pi 5 Pro 基线输出目录：`/home/wuage2335/linux-orangepi/out/orangepi5pro-livecfg-baseline`。
- 学习工程目录：`/home/wuage2335/linux-orangepi/ov13850_opi5pro_learning`。
- 板端部署暂存目录：`/home/orangepi/ov13850_opi5pro_learning/deploy/6.1.99-opi5pro-livecfg-baseline`。
- 仓库内 Codex 文档目录：`/home/wuage2335/linux-orangepi/docs/codex`。
- 新对话统一从 `docs/codex/HANDOFF.md` 和 `docs/codex/README.md` 进入，不再依赖 Windows 工作区才能恢复上下文。

不要参考或复用板端旧的 `~/ov13850_i2c_min`、旧自建 5 Pro overlay 工程作为正确实现。它们只可用于追溯历史，不能作为新实现基线。

## 4. 已验证的 6.1.99 启动基线

### 4.1 配置来源

从正常运行的 Orange Pi 5 Pro 旧内核通过 `/proc/config.gz` 导出配置，并在独立输出目录执行 `olddefconfig`。随后设置：

```text
CONFIG_LOCALVERSION="-opi5pro-livecfg-baseline"
# CONFIG_LOCALVERSION_AUTO is not set
# CONFIG_VIDEO_OV13850_I2C_MIN is not set
```

运行配置经当前 6.1.99 源码规范化后出现 148 项差异，说明原先 `out/orangepi5pro` 中配置不能直接视为可靠的 5 Pro 运行基线。

### 4.2 已验证产物

- kernel release：`6.1.99-opi5pro-livecfg-baseline`
- Image：`41329152` 字节
- Image SHA256：`2538b72cb4ad0576a56d411ea95fba4b55f0b98983ab4e5ff2fd922f049003bd`
- `bcmdhd.ko` SHA256：`51396e9d40b16beb50e41a9f7dffbaacc18c5847927ea992e78536c3c3d6be88`
- 新模块路径：`/lib/modules/6.1.99-opi5pro-livecfg-baseline/kernel/drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/bcmdhd.ko`

当前只部署了满足 Wi-Fi/SSH 验证的最小模块树，不是完整模块发行包。`depmod` 缺少 `modules.order` 和 `modules.builtin` 的警告来自该最小目录，不影响本次 `bcmdhd` 自动加载验证。

### 4.3 实机验收证据

```text
uname -
6.1.99-opi5pro-livecfg-baseline

wlan0  UP  192.168.1.17/24

bcmdhd 1486848 0
```

`modinfo bcmdhd` 指向新 release 目录，`vermagic` 为 `6.1.99-opi5pro-livecfg-baseline SMP mod_unload aarch64`。网关 `192.168.1.1` 两次 ping 均成功，SSH 和 NetworkManager 均为 active。

## 5. 启动问题复盘

### 5.1 第一次停在 `Starting kernel ...`

第一次部署同时替换了 Image、基础 DTB、overlay、uInitrd、模块目录和启动环境。U-Boot 已完成 Image/DTB 读取并跳转到内核，因此已基本排除 SD 卡容量、文件传输损坏、U-Boot 找不到文件和 6.1.99 Image 尺寸问题。

当前严谨结论：失败范围已缩小到第一次使用的内核配置、替换后的 DTB/overlay，或这些变量的组合；尚未取得足够证据定位到某一个 Kconfig 或 DT 节点。原因是当时 `earlycon=off`、`loglevel=1`，且 pstore/ramoops 没有留下崩溃记录。

后续如需追查，只能在当前可启动基线上一次引入一个变量，并保留可回滚备份。禁止再次整套替换后再猜测原因。

### 5.2 新内核启动但 SSH 不通

该问题已经完全确认：新内核实际已经进入用户空间，但 `/lib/modules/6.1.99-opi5pro-livecfg-baseline` 不存在，板载 Broadcom Wi-Fi 驱动 `bcmdhd.ko` 无法加载，系统只有 `lo`，没有 `wlan0`，所以 SSH 无法连接。

修复方法是只构建匹配新 release 的 `bcmdhd.ko`，安装到新模块目录并执行 `depmod -a 6.1.99-opi5pro-livecfg-baseline`。再次启动后 `bcmdhd` 自动加载、`wlan0` 获得原地址，SSH 恢复。

注意：Wi-Fi 实际使用 Broadcom `bcmdhd`，不是此前全量 `modules` 编译中报错的 Realtek 驱动。

## 6. 构建问题与源码状态

### 6.1 独立输出目录

所有 5 Pro 构建必须使用独立 `O=` 目录，不能污染源码树：

```bash
make O="$PRO_BASE_OUT" \
    ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    KCFLAGS="-Wa,-I,$PWD" \
    -j1 Image
```

Mali CSF 固件 `.incbin` 在独立输出目录构建时需要 assembler 搜索源码根目录，因此保留 `KCFLAGS="-Wa,-I,$PWD"`。

### 6.2 Realtek `O=` 路径修复

用户已修改以下文件，将依赖输出目录的 `-I$(src)/...` 改为基于源码树的 `-I$(srctree)/$(src)/...`：

- `drivers/net/wireless/rtl8189es/Makefile`
- `drivers/net/wireless/rtl8189fs/Makefile`
- `drivers/net/wireless/rtl8189es/hal/phydm/phydm.mk`
- `drivers/net/wireless/rtl8189fs/hal/phydm/phydm.mk`

ES/FS 目标对象已通过验证。完整 `modules` 构建仍会在其他 Realtek 驱动（已见 `rtl8192eu`）遇到同类厂商 Makefile 路径问题。该问题与板载 Wi-Fi 启动无关，暂不为摄像头阶段批量修复。

### 6.3 OV13850 学习驱动

学习驱动位于 `drivers/media/i2c/ov13850_i2c_min.c`，对应 `CONFIG_VIDEO_OV13850_I2C_MIN` 和 `learning,ov13850-i2c`。

已经完成并编译验证的修改包括：

- 将 `max_fps` 放入 `struct ov13850_mode`。
- 增加 `ov13850_min_from_client()`，正确处理 `v4l2_i2c_subdev_init()` 将 I2C clientdata 指向 `v4l2_subdev` 的语义。
- 修正 sysfs 回调和 `remove()` 的 clientdata 取回方式。
- `ov13850_min_s_stream` 和 `ov13850_enum_frame_interval` 已进入此前构建的 `vmlinux`。

当前可启动 baseline 特意关闭了 `CONFIG_VIDEO_OV13850_I2C_MIN`，目的是先证明系统启动与网络基线。下一步加入学习驱动时必须单独开启并验证。

## 7. 当前设备树与摄像头问题

板端 `/boot/orangepiEnv.txt` 当前关键配置：

```text
overlay_prefix=rk3588
fdtfile=rockchip/rk3588s-orangepi-5-pro.dtb
overlays=opi5pro-cam2 opi5pro-ov13850-new-overlay
```

新内核 baseline 没有与旧 `ovti,ov13850-new` overlay 匹配的传感器模块，因此 CIF 日志出现：

```text
update sensor info failed -19
get remote terminal sensor failed
```

这说明 CIF/DPHY 平台节点能够 probe，但 media graph 的远端 sensor 没有成功绑定。下一步应先读取实时设备树、I2C 节点和 endpoint，再决定：

1. 使用学习驱动的 `learning,ov13850-i2c`；或
2. 回到正式驱动的 `ovti,ov13850`。

一次只修改 compatible/驱动启用/overlay 中的一个验证变量，不同时替换基础 DTB 和全部启动组件。

## 8. 回滚与安全边界

- 已知旧内核 Image SHA256：`7a708246fb14a94cb10dad6d0b7cd0ab6c61e396bb3b706db406c341684dba52`。
- 已知历史备份目录：`/boot/ov13850-backup-before-6.1.99-ov13850-learning-20260717_233056`。
- 替换 `/boot/Image` 前始终创建带时间戳备份，并采用同文件系统内的临时文件加 `mv`。
- 未确认串口可用和回滚路径前，不同时修改 Image、DTB、uInitrd 和 overlay。
- 板端 root 操作由用户输入 sudo 密码；Codex 不请求或保存密码。

## 9. 下一步接手顺序

新对话按以下顺序继续：

1. 阅读仓库内 `docs/codex/README.md` 和 `docs/codex/HANDOFF.md`。
2. 确认板卡上电和网络状态，再通过 SSH 或串口验证 `uname -r`、`wlan0` 和 `bcmdhd`，确认仍处于已验证基线。
3. 读取板端实时设备树中 `camera@10`、原有 OV13850/OV13855 节点、endpoint 和 status。
4. 核对 `ov13850_i2c_min.c` 的 OF match、Kconfig/Makefile 和当前 baseline `.config`。
5. 明确本轮只验证一个 compatible/驱动绑定假设。
6. 用户理解并确认后再修改、编译、部署和实机验证。

不要重复基线内核和 Wi-Fi 问题的排查，也不要直接进入 ISP/MPP/RGA；先让 OV13850 sensor subdev 在 media graph 中稳定绑定。

## 10. 自动更新规则

- 调度时间：每天 06:00 至次日 00:00，每隔两小时更新一次。
- 永久保留“最高优先级协作约束”，不得改写为允许 Codex 默认完成整个学习阶段。
- 自动任务默认只更新本交接文档，不修改内核源码、任务计划或板端系统。
- 只记录由文件、构建输出、日志或实机状态证实的事实。
- 没有实质变化时只更新时间，不重复堆积相同内容。

## 11. 文档同步与版本控制

- 2026-07-20 起，Camera 项目的 Codex 接手文档同步到内核仓库 `docs/codex/`，并随 `linux-orangepi` Git 历史提交和推送。
- Windows 工作区 `C:\Users\Administrator\Documents\Camera开发` 仍可作为编辑来源，但供其他 Codex 对话接手时，以仓库内 `docs/codex/HANDOFF.md` 为入口。
- `docs/codex/` 应包含任务计划、发现记录、进度记录、完整项目计划、handoff 和 Orange Pi 5 Pro 内核故障手册。
- 更新 handoff 后应检查文档链接、UTF-8 编码和 `git diff --check`，再单独提交文档变更。
