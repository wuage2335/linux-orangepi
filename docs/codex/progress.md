# linux-orangepi 阅读进度

## 2026-07-15

- 已加载 `using-superpowers` 技能。
- 已加载 `planning-with-files-zh` 技能，用文件记录本次多步骤阅读任务。
- 已确认当前 Windows 共享工作区只有 `.agents` 和 `.git`，阅读记录将放在这里，暂不改动 WSL 项目。
- 尝试 `wsl.exe -l -v`、`wsl.exe --list --all --verbose` 和 `wsl.exe -e sh ...`，当前 Codex 进程看到的是无可用 WSL 发行版状态。
- 用户更正/确认 Windows 访问路径应为 `\\wsl.localhost\Ubuntu-22.04\home\wuage2335\linux-orangepi`；当前 Codex PowerShell 对该 UNC 路径执行 `Test-Path` 返回 `Access is denied`，所以此前使用 VHDX 离线读取是权限受限下的临时方案。
- 已确认 `C:\Users\Administrator\AppData\Local\Packages\CanonicalGroupLimited.Ubuntu22.04LTS_79rhkp1fndgsc\LocalState\ext4.vhdx` 存在，说明磁盘上有 Ubuntu WSL 数据文件，但当前未注册/未挂载。
- 尝试只读 `wsl --mount ... --options ro` 失败，错误为 `ERROR_SHARING_VIOLATION`，说明 VHDX 被 WSL 进程占用。
- 已创建 `tools/read_vhdx_ext4.py`，通过只读解析 VHDX 和 ext4 成功读取根目录与 `/home/wuage2335/linux-orangepi`。
- 已阅读顶层 `README`、`Makefile`、`boot.its`、`docs/orangepi5-ultra-kernel-reading-guide.md`、`docs/orangepi5-ultra-kernel-feature-map.md`。
- 已阅读主设备树 `rk3588-orangepi-5-ultra.dts`、DTB/DTBO Makefile、关键 defconfig 命中项、RKNPU Kconfig/Makefile、OV13850 sensor 驱动片段。
- 发现离线读取脚本对部分文件返回全零，已记录为当前方法限制。
- 已确认 WSL `main` 包含 `ov13850_i2c_min.c` 最小学习驱动；Windows partial 副本包含 `ov13850_new.c` 等不同实验分支内容。

## 2026-07-16

- 根据用户反馈，将当前任务从“项目通读”切换为“RK3588 摄像头低延迟链路实施”。
- 删除阶段中的天/周时间表示，统一改为阶段编号。
- 阶段 0“基线验证”和阶段 1“传感器与 DTS”已标记完成。
- 阶段 2“V4L2 驱动完善”设为当前阶段，后续阶段为 ISP/RGA、MPP、低延迟视频流、性能稳定性和可选 AI 扩展。
- 已将完整计划整理为独立文档 `docs/RK3588摄像头低延迟链路项目计划.md`，包含项目目标、阶段状态、阶段任务、验收标准、当前执行入口和环境说明。
- 已创建 `docs/对话任务交接文档.md`，用于新对话快速恢复目标、阶段状态、关键事实、环境限制和下一步操作。
- 首次创建每小时自动更新任务时，因状态值应为大写 `ACTIVE` 而被参数校验拒绝；已记录并准备使用正确枚举重试。
- 已成功创建每小时自动化“更新 RK3588 任务交接文档”，自动化标识为 `rk3588`，限定在当前 `Camera开发` 项目中运行并默认只修改交接文档。
- 核验时发现自动化首次落盘为每 2 小时，已显式修正为每 1 小时；本地配置现为 `ACTIVE` 且项目目标为当前 `Camera开发` 工作区。
- 用户将交接文档自动更新调整为每天 06:00 至 24:00 每两小时一次；已核验并同步为 00:00、06:00、08:00、10:00、12:00、14:00、16:00、18:00、20:00、22:00 触发，其中 00:00 对应前一日 24:00。
- 用户明确本项目是学习项目：用户是主要实践者，Codex 应以导师和结对伙伴方式推进；未经明确授权，不得一次性代写完整阶段、完整功能或大批量代码。该原则已写入任务计划、完整项目计划和交接文档。
- 已只读审查 WSL `main` 中的 `drivers/media/i2c/ov13850_i2c_min.c` 并对照正式 `ov13850.c`；确认学习驱动已经完成 bring-up 与 V4L2 Subdev 骨架，但仍有编译阻塞，且缺少 `s_stream`、Controls、runtime PM 和完整格式协商。阶段 2 已拆为 2A 至 2F，当前只推进 2A。
- 进一步确认 `v4l2_i2c_subdev_init()` 会改变 I2C clientdata 的对象类型；当前 sysfs 回调和 `remove()` 的直接私有结构体转换不成立，已将其纳入 2A 的基础装卸修正范围。

## 2026-07-18

- 从 Orange Pi 5 Pro 正常运行系统导出 `/proc/config.gz`，建立独立输出目录 `out/orangepi5pro-livecfg-baseline`，构建并验证 `6.1.99-opi5pro-livecfg-baseline`。
- 通过只替换 Image 的单变量测试确认 6.1.99 内核能够在 5 Pro 上启动；第一次停在 `Starting kernel ...` 的问题范围缩小到旧配置、替换后的 DTB/overlay 或组合不一致，尚未定位到单一根因。
- 确认新内核启动后 SSH 消失的直接原因是缺少对应 `/lib/modules/<release>`，导致板载 Broadcom `bcmdhd` 无法加载、`wlan0` 不存在。
- 定向构建并安装匹配新 release 的 `bcmdhd.ko`，执行 `depmod` 后 Wi-Fi、默认路由和 SSH 恢复。
- 在 `ov13850_opi5pro_learning` 下创建 `orangepi5pro-kernel-troubleshooting.md`，记录启动、模块、独立 `O=` 构建、回滚和摄像头绑定问题。

## 2026-07-20

- 开始将 Windows `Camera开发` 工作区中的 Camera 项目文档同步到 `linux-orangepi/docs/codex/`，作为其他 Codex 对话的统一接手入口。
- 更新 handoff，明确最近一次实机验证时间、当前 SSH 超时状态、仓库文档路径和下一步单变量验证顺序。
