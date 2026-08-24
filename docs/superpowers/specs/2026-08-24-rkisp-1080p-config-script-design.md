# RKISP 1080p 配置脚本设计

日期：2026-08-24  
状态：用户已确认设计

## 1. 目标

在 Orange Pi 5 Pro 的 OV13850 学习环境中，提供一个可重复、幂等的 Bash 脚本，
把当前 V4L2 pipeline 固定配置为：

```text
OV13850      2112x1568 SBGGR10
CSI/D-PHY    2112x1568 SBGGR10
CIF -> ISP   2112x1568 SBGGR10
ISP crop     left=0 top=190 width=2112 height=1188
mainpath     1920x1080 NV12
```

脚本成功后，`rkisp_mainpath` 可直接作为后续 MPP 的 1080p NV12 输入，不需要
RGA 参与这一条基线路径。

## 2. 非目标

- 不启动 stream，不采集文件，也不测 FPS。
- 不调用 MPP、RGA 或 RKAIQ。
- 不修改 Device Tree、内核配置、module 或启动文件。
- 不提供任意分辨率、任意 crop 或任意像素格式参数。
- 不写死 `/dev/video11`、`/dev/v4l-subdev2` 等动态编号。

## 3. 依赖与运行环境

- 运行位置：Orange Pi 5 Pro 板端。
- Shell：Bash。
- 外部工具：`v4l2-ctl`。
- 设备前提：`ov13850_i2c_min` 已内建并完成 media graph 注册。
- 权限前提：当前用户可打开相关 video/subdev 节点；脚本本身不要求 root。

脚本存放路径：

```text
ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh
```

## 4. 节点发现策略

脚本遍历 `/sys/class/video4linux/*/name`，按名称查找并映射到 `/dev/<basename>`：

| 角色 | sysfs name 匹配值 |
| --- | --- |
| Sensor | 包含 `ov13850_i2c_min` |
| CSI-2 D-PHY | `rockchip-csi2-dphy0` |
| MIPI CSI-2 | `rockchip-mipi-csi2` |
| CIF 到 ISP bridge | `rkcif-mipi-lvds` |
| ISP mainpath | `rkisp_mainpath` |

每个角色必须恰好匹配一个节点。零匹配或多匹配都视为错误，脚本不得猜测编号。

## 5. 配置顺序

1. 启用 Bash 严格错误处理。
2. 检查 `v4l2-ctl` 和 sysfs 根目录。
3. 自动发现五个节点并打印映射结果。
4. 依次把 Sensor、D-PHY、MIPI CSI-2、CIF bridge 的 pad 0 设置为
   `2112x1568 / MEDIA_BUS_FMT_SBGGR10_1X10`。
5. 把 mainpath video format 设置为 `1920x1080 NV12`。
6. 把 mainpath capture crop 设置为 `0,190 / 2112x1188`。
7. 读取 mainpath format 和 crop，验证实际值与目标完全一致。
8. 打印成功摘要并返回 0。

mainpath format 先于 crop 设置，避免某些驱动在 S_FMT 时重置 selection。脚本不设置
Sensor controls，沿用驱动当前/default 曝光、增益、VBLANK 和测试图状态。

## 6. 错误处理

- 工具缺失、sysfs 不存在、节点缺失或重复：打印角色和原因，返回非零。
- 任一 `v4l2-ctl` 调用失败：立即停止，不继续配置后续节点。
- readback 与目标不一致：打印目标值和实际输出，返回非零。
- 设备正在 streaming 导致 `EBUSY`：原样报告，不尝试强制停流。
- 不使用静默容错，不以旧配置继续运行。

## 7. 输出约定

正常输出包含：

```text
发现到的五个设备节点
每个配置步骤的简短名称
最终 1920x1080 NV12 format
最终 0,190 / 2112x1188 crop
CONFIGURATION_OK
```

错误输出写入 stderr，并包含 `ERROR:` 前缀。脚本不输出大量 `v4l2-ctl --all`
内容，只保留定位问题所需的信息。

## 8. 幂等性与状态边界

脚本连续运行两次应得到相同结果。第二次不得启动 stream、改变 runtime-PM usage，
也不得依赖第一次运行留下的 shell 变量。设备编号发生变化时，只要 sysfs name 不变，
脚本仍应找到正确节点。

## 9. 验证计划

1. `bash -n` 通过语法检查。
2. 在板端执行脚本一次，返回 0 且打印 `CONFIGURATION_OK`。
3. 第二次执行仍返回 0，证明幂等。
4. `v4l2-ctl -d <mainpath> --all` 显示：
   - `1920/1080`
   - `NV12`
   - `Bytes per Line: 1920`
   - `Size Image: 3110400`
   - crop `0,190 / 2112x1188`
5. 脚本执行后 Sensor runtime PM 保持 `suspended`、usage 0。
6. 脚本配置完成后，独立采集命令仍能稳定获得约 30 fps 的 1080p NV12。

## 10. 后续演进

MPP 最小编码程序成熟后，可以把同样的配置逻辑迁入应用初始化阶段；在此之前，
脚本作为可读、可复现的 bring-up 基线保留。只有出现旋转、额外缩放或 buffer 布局
不兼容需求时，才另行设计 RGA 路径。
