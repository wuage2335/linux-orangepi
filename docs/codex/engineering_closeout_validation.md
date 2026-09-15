# 2026-09-15 Camera 工程收口验证

## 2026-09-16：DFI修复后短回归通过

用户已安装`opi5pro-dfi-clocks` overlay并重启，启动备份为
`/boot/camera-closeout-backup-20260915-234106`。活动DFI节点已有四路时钟，
`/sys/class/devfreq/dmc`已出现，原来的DFI缺时钟错误和DMC deferred状态消失。
`/boot/Image` SHA256仍为`e5312723b9192fdb59fcf60b6770490e149888f8ec44d002cbde0ee5699d0f19`。

- 完整MPP/RGA/RTP/RTSP功能矩阵：`BOARD_REGRESSION_OK`。
- 3A RTSP运行180秒：4461帧、24.76fps、0采集timeout/drop；本机两客户端分别
  解码2238/2190帧，Windows D3D11另外成功解码90帧；合计3次连接/断开/IDR请求。
- RTSP主动丢弃28包发生于无客户端/重连窗口，不等于采集丢帧。
- 退出PM为suspended/0、cleanup_errors为空，新增3A内核日志无所检查的
  CIF/ISP/MPP/RGA/IOMMU/DFI/DMC fault/error/timeout/overflow。
- SoC温度45.307→55.461°C，180秒采样峰值59.153°C；收尾恢复controls为1536/16/96。

| DMC采样阶段 | 样本数 | 频率 | load范围 | load均值 |
| --- | ---: | --- | --- | ---: |
| 测前空闲20秒 | 20 | 534MHz | 0% | 0% |
| 功能矩阵（混合任务） | 75 | 534MHz / 2.4GHz | 0–32% | 2.12% |
| 3A会话（含启动和退出） | 185 | 534MHz / 2.4GHz | 0–12% | 3.77% |
| 测后空闲20秒 | 20 | 534MHz | 0% | 0% |

工作期间DMC升到2.4GHz，停流后回到534MHz。`load`来自DFI最忙通道的busy/total
计数比，接口只输出整数百分比；0%不表示绝对没有内存访问。这些数据不等同于
全DDR读写MB/s，也不能直接乘理论峰值冒充带宽实测。

启动仍有`failed to get vop bandwidth to dmc rate`及`failed to get vop pn to msch rl`
告警，但DMC已完成注册与运行；保留为显示/VOP相关观察项，不宣称启动日志零告警。

证据：板端`release/closeout-20260915/post-dfi-reboot/`；Windows
`engineering-closeout-work/artifacts/post-dfi-reboot/`，含`analysis.json`、原始DMC
采样、两个阶段日志与内核日志。Windows解码日志为`post-reboot-windows-d3d11.log`。

本轮重启后回归完成。仍未完成：精确光学同屏显示延迟、绝对DDR读写MB/s测量。
下文保留修复前的构建与测量过程。

代码基线：`3c7da6a37`。板卡：Orange Pi 5 Pro / RK3588S，当前 SSH 地址
`192.168.1.16`，内核 `6.1.99-opi5pro-livecfg-baseline`。本轮应用部署在独立目录：

`/home/orangepi/ov13850_opi5pro_learning/release/closeout-20260915`

## 1. 构建与入口

- WSL Ubuntu 22.04 已安装 GStreamer 1.20.3、RTSP server 1.20.1 的开发包，
  Doxygen 1.9.1；从固定 MPP commit `c08762ebfadeb4e986d2fed993bc7a54862d3ebe`
  构建独立 x86_64 SDK，完整链接 RTP、RTSP 和 benchmark。
- 板端使用相同版本 aarch64 MPP bundle，完整原生构建通过。
- librga 从已固定 commit `2b32edcb97b601b25683e2941d888c8515da6d55` 恢复，
  SHA256 `e150bda757fb5e8a649c429ec7cabaf851aa2a3be554ed494d5519e8790d943b`；
  两个 RGA 应用以 `-Wall -Wextra -Werror` 交叉编译成功。
- RKAIQ host 测试、兼容工具 aarch64 构建、RKISP 配置脚本测试通过。
- streaming Doxygen 已由真实工具解析，`WARN_AS_ERROR=YES`，退出码0。

新增入口均位于 `ov13850_opi5pro_learning/streaming/`：

| 入口 | 用途 |
| --- | --- |
| `scripts/build_host.sh` | 固定版 host MPP SDK + streaming/benchmark 编译测试 |
| `scripts/build_board.sh` | aarch64 原生构建与本机测试，活动会话期间拒绝构建 |
| `scripts/package_closeout.sh` | 打包源码、aarch64 MPP SDK、RGA 程序和完整回归脚本 |
| `scripts/camera_session.py` | 一键启动 3A、配置ISP、RTSP；支持时限、解码重连与证据保存 |
| `scripts/run_board_regression.sh` | 官方/自有MPP、H.264/H.265、RGA、RTP/RTSP功能矩阵 |
| `scripts/summarize_session.py` | 从原始 /proc 和温度记录重算 CPU/RSS/温度 |

## 2. 已完成的功能验证

`regression-matrix-v2.log` 末尾为 `BOARD_REGRESSION_OK`，`status.txt` 为
`exit_code=0`。具体覆盖：

1. RKISP 配置、跳过10帧后采集3,110,400字节NV12。
2. 官方 MPP 生成30帧、自有H.264文件编码300帧、H.264 CBR/VBR及H.265矩阵。
3. V4L2 copy/DMA-BUF各300帧、H.264/H.265官方硬件解码。
4. RGA文件缩放重复输出、实时copy/direct各300帧。
5. RTP发送300帧，0 timeout/drop/queue overrun。
6. RTSP两次连接、解码及IDR恢复，退出PM为suspended/0。

另有60秒3A实景回归：1179帧、19.59fps、0 timeout/drop；两客户端分别解码
499/657帧，2次连接/断开/IDR；停流后PM为suspended/0。
`rtsp_dropped_packets=19` 来自无客户端/重连窗口的主动丢弃，不等于采集丢帧。

## 3. 长稳与受控对照

固定曝光600秒对照已通过：18011帧、30.05fps、0 timeout/drop；两客户端分别解码
9005/8969帧，2次连接/断开/IDR；退出PM为suspended/0。
发送进程CPU平均3.194%（单核口径），RSS最高16272KiB；SoC温度49.0→58.23°C、
最高59.153°C。配置固定为exposure=1536、analogue_gain=16、VBLANK=96。

最终3A1800秒复测已通过：44591帧、24.76fps、0 timeout/drop；两客户端分别解码
22285/22271帧，2次连接/断开/IDR，退出PM为suspended/0且cleanup_errors为空。
发送进程CPU平均3.344%，RKAIQ平均3.689%；RSS最高分别30028/13392KiB。
SoC温度54.538→59.153°C，最高60.076°C；全部温区的最高值为大核61.923°C。
本轮照度下AE使用exposure=1997、VBLANK=451，因此真实帧率低于固定曝光组。
共1790条资源采样覆盖约1800秒（采样和中途重连也消耗时间），不是逐帧采样。

| 条件 | 发送帧数 | 实际FPS | 采集timeout/drop | server CPU | RKAIQ CPU | SoC最高温度 |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| 固定曝光，600s | 18011 | 30.05 | 0 / 0 | 3.194% | 未运行 | 59.153°C |
| 3A实景，1800s | 44591 | 24.76 | 0 / 0 | 3.344% | 3.689% | 60.076°C |

3A长稳新增内核日志未检出CIF/ISP/MPP/RGA/IOMMU的fault/error/timeout/overflow。
无3A对照启动时有一次`waiting on params stream on event timeout`，随后正常开流，
符合未启动RKAIQ的参数等待条件；该告警已保留，不能写成全程零内核告警。
历史72ms样本不作为本轮3A显示延迟。30分钟通过也不等于24小时稳定性验收。

温度与 CPU/RSS 每秒采样。CPU 采用单核100%口径，`CLK_TCK=100`；板端同时
运行软件解码客户端，因此其CPU与发端/RKAIQ分别报告。两组顺序执行，未控制
冷却与环境温度，不能由峰值差直接推断3A热功耗。

## 4. 本轮发现并修复的工程问题

首次1800秒测试在约663秒退出，server返回`-7`（SIGBUS）。同一时刻执行了
`build_board.sh`，旧Makefile的`runtime-lib`用`cp -a`原地覆盖了运行中映射的MPP库。
库ctime为22:38:22，构建日志也在22:38:22开始。该测试受到部署操作干扰，失败
记录保留在`soak-3a-1800s/`，不作为自然运行的稳定性结论。

修复措施：

- `install_mpp_runtime.py` 在目标目录暂存后使用`os.replace`发布，保留旧inode。
- 实际`mmap`测试验证旧映射内容不变、目标inode改变、新读取得到新内容。
- 板端构建入口与会话共用flock，避免构建与摄像头会话并行。
- 会话测试验证定时退出、信号退出、配置失败、RKAIQ早退、就绪超时、互斥、
  目录不覆盖与构建拒绝活动锁。
- 独立审查后增加孤儿进程组清理、进程退出竞态容错、PM读取容错和逐秒解码进度
  检查；连续10秒无新解码帧直接失败。当前12项新增测试全部通过。

`soak-3a-1800s-v2`在脚本补强前已启动，随后主动停止并由
`soak-3a-1800s-final`替代，不能把该中断组计作长稳通过。

H.265首次解码测试误用了类型167，已依据`rk_type.h`更正为16777220并通过复测。

## 5. DDR带宽与驱动遗留项

活动内核启用了`CONFIG_DEVFREQ_EVENT_ROCKCHIP_DFI=y`，但活动DT的
`/dfi@fe060000`缺少`clocks`和`clock-names`。当前驱动要求
`pclk_ddr_mon_ch0..3`，启动日志为：

```text
rockchip-dfi fe060000.dfi: Failed to get pclk_ddr_mon_ch0
rockchip-dfi: probe of fe060000.dfi failed with error -22
platform dmc: deferred probe pending
```

因此修复前没有可用DFI/DMC计数入口。候选overlay仅补齐四路时钟，先通过dtc编译
及对板端基础DTB副本的fdtoverlay验证，随后由用户部署重启，实机结果见本文顶部。
候选SHA256：
`3a55fd7e2989a97319cc5018d322935e7c37f4c5971f7b7c7dff845fa8b8c21e`。

用户确认串口可回滚，并已交互执行sudo部署与重启。部署脚本含等待会话锁、原文件
哈希校验与启动配置备份。
脚本的备份、哈希拒绝与活动锁拒绝已在临时启动目录通过3项测试。
候选源码和部署辅助脚本位于`docs/codex/diagnostics/`。脚本锁定修复前文件哈希，
安装成功后不能重复执行同一初次部署流程；已安装状态以活动设备树和实测为准。

control event subscription和未接OV13855节点不阻塞本轮应用链路。当前学习驱动
core ops确实没有订阅回调；仍保留42/43合规边界，不宣称全部通过。

## 6. 光学显示延迟

Windows软件解码和D3D11解码已取得1920x1080实景截图。首次取帧出现近黑画面，
持续接收后有正常画面；截图只证明解码可用，不证明端到端延迟。
本轮浏览器界面控制被工具因URL识别不确定停止；计时页面已准备在本机环回地址，
尚未取得可计算的源时钟/接收显示同屏截图，因此不填写毫秒值。

## 7. 证据位置

板端相对`release/closeout-20260915/`：`native-build.log`、`board-build-safe.log`、
`regression-60s-v2/`、`regression-matrix-v2/`、`fixed-600s/`、`soak-3a-1800s-final/`。

Windows工作区：`engineering-closeout-work/artifacts/`，包含构建日志、部署包、
Doxygen XML、收到的PNG及DFI离线验证产物。所有未测项目与失败运行保留原记录。

最终原始记录已回传为`artifacts/final-measurements.tar.gz`，两组解析结果保存在
解压目录下各自的`resource-summary.json`。实机收尾已恢复本轮起始controls：
exposure=1536、analogue_gain=16、VBLANK=96，sensor PM suspended/0。
