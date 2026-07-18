# Orange Pi 5 Pro 内核问题与修复记录

> 适用项目：`linux-orangepi/ov13850_opi5pro_learning`  
> 板卡：Orange Pi 5 Pro（RK3588S）  
> 文档用途：持续记录内核构建、部署、启动、模块和摄像头 bring-up 中遇到的问题、证据、修复方法与验证结果。  
> 最后更新：2026-07-18

## 1. 当前已验证基线

### 1.1 主机与板端

- WSL：`Ubuntu-22.04`
- WSL 进入命令：`wsl -d Ubuntu-22.04`
- 内核源码：`/home/wuage2335/linux-orangepi`
- 独立输出目录：`/home/wuage2335/linux-orangepi/out/orangepi5pro-livecfg-baseline`
- 学习目录：`/home/wuage2335/linux-orangepi/ov13850_opi5pro_learning`
- 板端 SSH：`orangepi@192.168.1.17`
- 板端串口：`COM20`，`1500000 8N1`

### 1.2 当前可启动内核

```text
kernel release: 6.1.99-opi5pro-livecfg-baseline
Image size:     41329152 bytes
Image SHA256:   2538b72cb4ad0576a56d411ea95fba4b55f0b98983ab4e5ff2fd922f049003bd
```

当前已验证：

- 6.1.99 内核可以在 Orange Pi 5 Pro 上启动并进入用户空间。
- 串口控制台正常。
- `bcmdhd` 能自动加载。
- `wlan0` 能获得 `192.168.1.17/24`。
- SSH 和 NetworkManager 正常。
- 当前只部署了满足 Wi-Fi/SSH 的最小模块树，不是完整的内核模块发行包。

## 2. 问题一：串口停在 `Starting kernel ...`

### 2.1 现象

U-Boot 日志能够走到：

```text
Booting using the fdt blob at 0x0a100000
Using Device Tree in place ...
Starting kernel ...
```

随后没有新的串口日志，LED 没有进入正常状态，SSH 也无法连接。

### 2.2 当时一次性改变的内容

第一次部署同时更换或修改了：

- `/boot/Image`
- 基础 DTB
- Camera DT overlay
- `/boot/uInitrd`
- `/lib/modules/<release>`
- `/boot/orangepiEnv.txt` 中的 overlay 配置
- 内核 `.config`

这是本次问题最重要的流程缺陷：变量过多，启动失败后无法通过一次实验确定责任组件。

### 2.3 已排除的原因

以下判断已有实机证据支持：

1. **不是 `/boot` 空间不足。** 当时 `/boot` 仍有约 396 MiB 可用，Image、DTB、overlay 和 uInitrd 均能完整写入。
2. **不是文件传输损坏。** 部署包四个文件执行 `sha256sum -c SHA256SUMS` 均为 `OK`。
3. **不是 U-Boot 找不到 Image 或 DTB。** U-Boot 已完成 Image/DTB 加载并打印 `Starting kernel ...`。
4. **不是 6.1.99 或 38~39 MiB Image 天生不能启动。** 后续 `6.1.99-opi5pro-livecfg-baseline` 在只替换 Image 的情况下启动成功。
5. **不是 SD 卡容量大小本身导致。** 恢复旧 Image 后，相同 SD 卡和根文件系统能够正常启动。

### 2.4 尚未精确确认的根因

当前只能把第一次失败缩小到以下范围：

- 第一次使用的内核配置不适合当前 Orange Pi 5 Pro；或
- 当时替换的基础 DTB/overlay 存在不兼容；或
- Image、DTB、overlay、uInitrd 和模块版本组合不一致。

目前不能写成“已经确定是某个 Kconfig”或“已经确定是某个 DT 节点”。当时缺少足够的早期内核证据：

```text
earlycon=off
loglevel=1
```

而 `/sys/fs/pstore` 和 `/var/lib/systemd/pstore` 中也没有留下崩溃记录。

### 2.5 排查方法

1. 恢复已知可启动的 6.1.43 Image，确认硬件、SD 卡、U-Boot、DTB 和根文件系统仍然可用。
2. 从正在运行的 5 Pro 导出真实配置：

   ```bash
   zcat /proc/config.gz > .config.imported
   ```

3. 建立全新的独立输出目录，避免复用来源不明的 `out/orangepi5pro`。
4. 执行 `olddefconfig`，发现导入配置经过当前源码规范化后有 148 项差异。
5. 设置独立 release 名称并关闭学习驱动，先验证纯启动基线：

   ```text
   CONFIG_LOCALVERSION="-opi5pro-livecfg-baseline"
   # CONFIG_LOCALVERSION_AUTO is not set
   # CONFIG_VIDEO_OV13850_I2C_MIN is not set
   ```

6. 只构建并部署新 Image，保留原 DTB、overlay、uInitrd 和根文件系统。
7. 串口确认新内核成功进入 shell。

### 2.6 后续追查原则

如需继续定位第一次 `Starting kernel` 的唯一根因，只能从当前可启动基线开始，一次引入一个变量：

1. 先保持 Image 不变，只验证目标 overlay。
2. 再保持 overlay 不变，只启用学习驱动。
3. 基础 DTB 必须最后单独验证。
4. 每次部署前保留 Image、DTB、uInitrd 和环境文件备份。
5. 调查早期启动问题时临时提高日志等级并启用适合 RK3588 的 early console。

## 3. 问题二：新内核已启动，但 SSH 无法连接

### 3.1 现象

串口能够进入用户空间，并显示：

```text
uname -r
6.1.99-opi5pro-livecfg-baseline
```

但是 SSH 无法连接。串口检查发现：

```text
/sys/class/net
└── lo
```

`sshd` 和 NetworkManager 均为 active，但系统没有 `wlan0`，也没有 IP 和默认路由。

### 3.2 根因

板端不存在新 release 的模块目录：

```text
/lib/modules/6.1.99-opi5pro-livecfg-baseline
```

Orange Pi 5 Pro 当前板载 Wi-Fi 使用 Broadcom `bcmdhd`：

```text
/lib/modules/6.1.43-rockchip-rk3588/kernel/drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/bcmdhd.ko
```

旧模块的 `vermagic` 是 `6.1.43-rockchip-rk3588`，不能给 6.1.99 内核使用。因此故障链路为：

```text
缺少新 release 模块树
-> bcmdhd 无法加载
-> wlan0 没有创建
-> 没有 192.168.1.17
-> SSH 无法连接
```

注意：这一次内核已经正常启动，不能把“SSH 不通”直接等同于“内核未加载”。

### 3.3 定向构建 Wi-Fi 模块

没有先修复所有厂商模块，而是只构建板卡实际需要的 `bcmdhd.ko`：

```bash
cd ~/linux-orangepi

PRO_BASE_OUT="$PWD/out/orangepi5pro-livecfg-baseline"

make O="$PRO_BASE_OUT" \
    ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    KCFLAGS="-Wa,-I,$PWD" \
    -j1 \
    drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd/bcmdhd.ko
```

验证结果：

```text
bcmdhd.ko SHA256:
51396e9d40b16beb50e41a9f7dffbaacc18c5847927ea992e78536c3c3d6be88

vermagic:
6.1.99-opi5pro-livecfg-baseline SMP mod_unload aarch64

depends:
空
```

### 3.4 板端安装

```bash
REL=6.1.99-opi5pro-livecfg-baseline
STAGE="$HOME/ov13850_opi5pro_learning/deploy/$REL"
DEST="/lib/modules/$REL/kernel/drivers/net/wireless/rockchip_wlan/rkwifi/bcmdhd"

sudo install -d -m 0755 "$DEST"
sudo install -m 0644 "$STAGE/modules/bcmdhd.ko" "$DEST/bcmdhd.ko"
sudo depmod -a "$REL"
```

最小模块树缺少 `modules.order` 和 `modules.builtin` 时，`depmod` 会打印警告。对于本次只有一个无依赖的外部模块测试，该警告不阻止生成 `modules.dep` 和 `modules.alias`，也不阻止 `bcmdhd` 自动加载。完整发行包仍应安装完整模块元数据。

### 3.5 修复验证

```bash
uname -r
lsmod | grep '^bcmdhd'
modinfo bcmdhd | grep -E '^(filename|depends|vermagic):'
ip -br addr show wlan0
ip route
ping -c 2 192.168.1.1
systemctl is-active ssh NetworkManager
```

实机结果：

```text
6.1.99-opi5pro-livecfg-baseline
wlan0 UP 192.168.1.17/24
2 packets transmitted, 2 received, 0% packet loss
ssh: active
NetworkManager: active
```

## 4. 问题三：设置 `CONFIG_LOCALVERSION` 后 `kernelrelease` 没变化

### 4.1 现象

执行 `scripts/config --set-str LOCALVERSION ...` 和 `olddefconfig` 后：

```text
.config: CONFIG_LOCALVERSION="-ov13850-learning"
kernelrelease: 6.1.99
```

### 4.2 原因

输出目录中的自动配置文件尚未同步，`include/config/auto.conf` 仍保留旧值。

### 4.3 修复

```bash
make O="$OUT" ARCH=arm64 syncconfig

grep '^CONFIG_LOCALVERSION' "$OUT/include/config/auto.conf"
make O="$OUT" ARCH=arm64 kernelrelease
```

验证后应得到：

```text
6.1.99-ov13850-learning
```

发布前必须同时核对 `.config`、`auto.conf` 和 `kernelrelease`，不能只看 `.config`。

## 5. 问题四：独立 `O=` 构建时 Mali 固件文件找不到

### 5.1 现象

```text
Assembler messages:
Error: file not found: drivers/gpu/arm/bifrost/mali_csffw.bin
```

### 5.2 原因

Mali 驱动使用 assembler `.incbin` 引用源码树中的固件文件。独立输出目录构建时，assembler 默认搜索路径位于输出树，无法解析相对源码路径。

### 5.3 修复

构建时给 assembler 增加源码树搜索路径：

```bash
KCFLAGS="-Wa,-I,$PWD"
```

例如：

```bash
make O="$PRO_BASE_OUT" \
    ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    KCFLAGS="-Wa,-I,$PWD" \
    -j1 Image
```

这属于厂商源码对独立输出目录支持不完整，不是 Mali 固件真的缺失。

## 6. 问题五：独立 `O=` 构建时 Realtek 头文件找不到

### 6.1 现象

已出现的错误包括：

```text
fatal error: drv_types.h: No such file or directory
fatal error: rtl8188f/halhwimg8188f_mac.h: No such file or directory
```

涉及 `rtl8189es`、`rtl8189fs`，完整 `modules` 构建后续还在 `rtl8192eu` 遇到同类问题。

### 6.2 原因

厂商 Makefile 使用：

```make
-I$(src)/include
-I$(src)/platform
```

在独立 `O=` 构建下，部分路径会相对于输出树解析，而头文件实际位于源码树。

### 6.3 已完成修复

以下文件已将 include 路径改为基于源码根目录：

- `drivers/net/wireless/rtl8189es/Makefile`
- `drivers/net/wireless/rtl8189fs/Makefile`
- `drivers/net/wireless/rtl8189es/hal/phydm/phydm.mk`
- `drivers/net/wireless/rtl8189fs/hal/phydm/phydm.mk`

核心形式为：

```make
-I$(srctree)/$(src)/include
-I$(srctree)/$(src)/platform
-I$(srctree)/$(src)/hal/phydm
```

ES/FS 定向目标已经编译通过。完整 `modules` 仍可能在其他 Realtek 目录遇到同类问题，应按具体报错逐个修复，不能全局注入某个芯片目录的 include 路径。

### 6.4 与当前 Wi-Fi 的关系

板载 Wi-Fi 实际使用 `bcmdhd`，因此 Realtek 全模块编译失败不是当前 SSH 恢复的阻塞项。不要为了摄像头 bring-up 先批量修完所有未使用的无线驱动。

## 7. 问题六：编译 warning 被当作 error

### 7.1 现象

摄像头相关目标构建中，某个变量声明后未使用，只显示 warning，但上层 make 最终以 Error 2 退出。

### 7.2 原因

当前厂商内核构建配置会将部分 warning 提升为 error。最后几行通常只有上层目录的 `Error 2`，真正的首个错误位于更早的日志中。

### 7.3 排查方法

不要只看日志尾部。使用单线程和日志文件定位第一个错误：

```bash
make O="$OUT" \
    ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    -j1 Image 2>&1 | tee "$OUT/build-j1.log"

rg -n -i \
    'warning:|fatal error:|error:|undefined reference|No rule to make target|file not found' \
    "$OUT/build-j1.log"
```

修复源代码中的未使用变量或声明位置，不要通过全局关闭 `-Werror` 掩盖问题。

## 8. 问题七：`v4l2_i2c_subdev_init()` 后错误解释 clientdata

### 8.1 风险

`v4l2_i2c_subdev_init()` 会让 I2C clientdata 指向 `struct v4l2_subdev`。如果 sysfs 回调或 `remove()` 仍将 `i2c_get_clientdata()` 直接解释成 `struct ov13850_min *`，就会得到错误指针，存在崩溃或内存破坏风险。

### 8.2 修复

学习驱动增加统一转换函数：

```c
static inline struct ov13850_min *
ov13850_min_from_client(struct i2c_client *client)
{
    struct v4l2_subdev *sd = i2c_get_clientdata(client);

    return to_ov13850_min(sd);
}
```

sysfs 回调和 `remove()` 统一通过该函数取回私有结构体。对应目标对象和 Image 已编译通过。

## 9. 问题八：CIF 报远端 sensor 获取失败

### 9.1 当前日志

```text
update sensor info failed -19
rkcif_update_sensor_info: get remote terminal sensor failed
```

### 9.2 当前配置

```text
overlay_prefix=rk3588
fdtfile=rockchip/rk3588s-orangepi-5-pro.dtb
overlays=opi5pro-cam2 opi5pro-ov13850-new-overlay
```

### 9.3 当前判断

CIF/DPHY 平台驱动能够 probe，但 media graph 远端 sensor 没有成功绑定。当前 baseline 关闭了 `CONFIG_VIDEO_OV13850_I2C_MIN`，旧 overlay 使用的 `ovti,ov13850-new` 也没有匹配当前 baseline 中的传感器驱动。

这属于摄像头驱动与设备树绑定问题，不属于内核启动或 Wi-Fi 问题。

### 9.4 下一步排查

1. 读取板端实时设备树中 camera、OV13850/OV13855、DPHY、CSI 和 endpoint 节点。
2. 检查每个节点的 `status`、`compatible`、`remote-endpoint` 和端口方向。
3. 核对学习驱动 `of_match_table`、Kconfig 和 Makefile。
4. 明确本轮使用 `learning,ov13850-i2c` 还是正式驱动 `ovti,ov13850`。
5. 一次只改变 compatible、驱动启用或 overlay 中的一个变量。

在完成上述证据收集前，不直接重写整份基础 DTB。

## 10. 内建驱动与外部模块的边界

将传感器驱动配置为 `y` 能保证驱动随内核启动并尽早参与设备匹配，但“传感器作为外部模块”本身不会直接让 MIPI/CSI 节点被设备树关闭。

应区分：

- DT 节点是否启用：由 `status = "okay"` 和 overlay 决定。
- 平台驱动是否存在：由对应 Kconfig 和内核/模块部署决定。
- sensor 是否绑定：由 I2C 设备、`compatible`、驱动 match 和 probe 结果决定。
- media graph 是否闭合：由 endpoint 和 `remote-endpoint` 决定。

本学习项目可以将 `ov13850_i2c_min` 编进内核，以减少部署变量；但仍必须单独验证 DT 节点和 endpoint，不能把所有链路问题归因于 `y` 或 `m`。

## 11. 推荐的安全部署流程

### 11.1 构建前

```bash
make O="$OUT" ARCH=arm64 syncconfig
make O="$OUT" ARCH=arm64 kernelrelease
grep '^CONFIG_LOCALVERSION' "$OUT/include/config/auto.conf"
```

确认 release 与准备安装的 `/lib/modules/<release>` 完全一致。

### 11.2 生成发布目录

发布目录至少包含：

- `Image`
- 目标 DTB（只有本轮确实修改时才包含）
- 目标 overlay（只有本轮确实修改时才包含）
- `modules-<release>.tar.gz` 或明确记录的最小模块集合
- `SHA256SUMS`

传输后先执行：

```bash
sha256sum -c SHA256SUMS
```

### 11.3 替换前备份

```bash
REL=<new-release>
BACKUP="/boot/kernel-backup-before-$REL-$(date +%Y%m%d_%H%M%S)"

sudo install -d -m 700 "$BACKUP"
sudo cp -a \
    /boot/Image \
    /boot/uInitrd \
    /boot/orangepiEnv.txt \
    /boot/dtb/rockchip/rk3588s-orangepi-5-pro.dtb \
    "$BACKUP/"
```

### 11.4 单变量替换 Image

```bash
sudo install -m 0755 "$STAGE/Image" /boot/Image.new
sha256sum "$STAGE/Image" /boot/Image.new
sudo mv /boot/Image.new /boot/Image
sudo sync
```

确认哈希一致后再重启。不要在同一次实验中同时更换 Image、DTB、overlay 和 uInitrd。

### 11.5 启动后检查顺序

1. 串口是否出现内核日志和登录提示。
2. `uname -r` 是否为目标 release。
3. `/lib/modules/$(uname -r)` 是否存在。
4. 必需模块是否来自正确 release。
5. `ip -br addr` 是否出现目标接口。
6. 再检查 SSH、camera media graph 和视频节点。

## 12. 已知回滚信息

- 旧内核 release：`6.1.43-rockchip-rk3588`
- 旧 Image SHA256：`7a708246fb14a94cb10dad6d0b7cd0ab6c61e396bb3b706db406c341684dba52`
- 已知历史备份目录：

  ```text
  /boot/ov13850-backup-before-6.1.99-ov13850-learning-20260717_233056
  ```

新部署必须再创建独立的时间戳备份，不要反复覆盖这一份历史备份。

## 13. 新问题记录模板

后续遇到问题时，在本文末尾按以下模板追加：

```markdown
## 问题 N：简短标题

### 现象

### 环境与版本

### 复现步骤

### 证据与日志

### 根因

### 修复方法

### 验证命令与结果

### 回滚方法

### 结论边界与遗留问题
```

只有得到日志、构建输出或实机验证支持后，才能填写“根因”；尚未证实的内容必须标注为假设。
