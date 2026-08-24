# Orange Pi 5 Pro RGA NV12 文件缩放实验设计

日期：2026-08-24

## 1. 背景

OV13850 学习驱动、CIF、RKISP 链路已经能够稳定输出
1920x1080 NV12，持续采集速率约为 30 fps。阶段 3 的下一项任务是学习
RK3588S RGA 用户态接口，为后续把 RGA 接入实时 V4L2/DMA-BUF 链路打基础。

板端已经确认：

- 设备：Orange Pi 5 Pro（RK3588S）；
- RGA 设备节点：`/dev/rga`；
- RGA 内核驱动：`RGA multicore Device Driver: v1.3.7`；
- 当前系统没有安装 `librga.so`、IM2D 头文件或 RGA 示例程序。

官方 librga 说明当前 API 最低适配多核驱动 v1.2.4，建议使用
v1.3.13 及以上驱动。板端 v1.3.7 满足最低要求，因此本实验先保持内核不变，
通过固定用户态库版本、运行前检查和明确错误输出来控制兼容性风险。

## 2. 目标

建立一个最小、可复现的文件到文件 RGA 实验：

```text
1920x1080 NV12 文件
        |
        v
官方 librga IM2D / imresize
        |
        v
1280x720 NV12 文件
```

实验需要证明：

1. 官方 aarch64 librga 能在当前 RK3588S 系统加载；
2. IM2D 能通过 RGA 完成 NV12 缩放；
3. 输出尺寸和内容合理；
4. 能记录单次以及固定循环次数下的 RGA 调用耗时；
5. 所有依赖和运行命令都能由后续对话复现。

## 3. 非目标

第一版不处理以下内容：

- 不直接连接摄像头实时流；
- 不导入 V4L2 DMA-BUF fd；
- 不实现多线程流水线；
- 不做颜色格式转换、旋转、叠加或裁剪；
- 不升级板端 RGA 内核驱动；
- 不把 librga 安装到 `/usr` 或 `/usr/local`；
- 不以 RGA 替代已经验证成功的 RKISP 1920x1080 缩放基线。

文件实验通过后，再单独设计 V4L2/DMA-BUF 到 RGA 的实时实验。

## 4. 官方依赖

使用 Rockchip 官方仓库：

- 仓库：`https://github.com/airockchip/librga`；
- API 版本：`1.10.6`；
- 固定提交：`2b32edcb97b601b25683e2941d888c8515da6d55`；
- 目标库：`libs/Linux/gcc-aarch64/librga.so`；
- 头文件来源：`include/`；
- 许可证：Apache License 2.0，保留原仓库 `COPYING`；
- 目标架构：64 位 Linux aarch64，官方说明适用于 RK3588。

只把实验需要的官方头文件、动态库、许可证和来源清单放入项目目录。
来源清单必须记录仓库、提交、API 版本和原始路径，禁止用未注明来源的板端库替换。

## 5. 目录设计

```text
ov13850_opi5pro_learning/rga/
├── README.md
├── Makefile
├── src/
│   └── rga_nv12_resize.cpp
├── tests/
│   └── test_rga_nv12_resize.sh
└── third_party/librga/
    ├── ORIGIN.md
    ├── COPYING
    ├── include/
    └── lib/aarch64/librga.so
```

`third_party/librga` 只负责保存固定的官方依赖；`src` 只负责实验程序；
`tests` 只负责输入校验和结果验收。实时摄像头程序以后放在独立文件中，
不向这个最小程序持续叠加功能。

## 6. 程序接口与数据约束

程序只接受两个位置参数：

```text
rga_nv12_resize <input-1920x1080.nv12> <output-1280x720.nv12>
```

固定格式与尺寸：

- 输入：NV12，1920x1080，期望大小 3,110,400 字节；
- 输出：NV12，1280x720，期望大小 1,382,400 字节；
- 输入、输出宽高都必须为偶数；
- 第一版不提供任意尺寸、任意格式或复杂命令行选项。

固定尺寸可以让第一次实验集中验证 librga、RGA 驱动和 NV12 内存布局，
避免把参数解析和通用图像约束混入硬件接口调试。

## 7. 处理流程

程序执行顺序如下：

1. 检查参数数量和输入文件大小；
2. 为输入、输出分别申请一段连续的进程虚拟地址空间；
3. 完整读取一帧 NV12 输入，并清零输出缓冲区；
4. 用官方 `importbuffer_virtualaddr` 各导入一次源和目标缓冲区；
5. 用 `wrapbuffer_handle` 把导入 handle 包装成源、目标 `rga_buffer_t`；
6. 用 `imcheck` 检查格式、尺寸和操作组合；
7. 先执行 5 次不计时预热，再执行 100 次计时缩放；
8. 每次调用使用 `imresize`，失败时通过官方错误字符串输出原因；
9. 把最后一次完整输出写入目标文件；
10. 输出 librga API 信息、输入输出描述、循环次数和耗时统计；
11. 用 `releasebuffer_handle` 释放两个导入 handle，再释放进程缓冲区；
12. 返回明确的退出码。

计时只覆盖 RGA 缩放调用，不把文件读取和写入时间混入硬件处理耗时。
缓冲区导入也位于计时区间之外，避免 100 次循环反复混入地址导入开销。
计时采用单调时钟，报告总耗时、平均耗时和推算吞吐率。该结果用于学习和
同机比较，不当作严格的端到端摄像头延迟。

## 8. 动态库加载

构建时链接项目内 `librga.so`。可执行文件的 RPATH 固定为
`$ORIGIN/../lib`，使板端按照可执行文件所在目录寻找随包部署的库，
不依赖系统全局 librga。

部署包至少包含：

```text
bin/rga_nv12_resize
lib/librga.so
```

运行前用 `ldd` 验证实际解析到部署包中的库。这样不同对话、不同板端环境
不会因为 `/usr/local/lib` 中残留了另一个版本而得到不可复现的结果。

## 9. 错误处理

以下情况必须失败并返回非零状态：

- 参数数量不正确；
- 输入文件不存在、无法读取或大小不是 3,110,400 字节；
- 缓冲区申请失败；
- `imcheck` 拒绝当前操作；
- `imresize` 返回失败；
- 输出文件无法创建或没有写满 1,382,400 字节；
- 动态库无法加载或当前架构不匹配。

错误信息必须包含失败阶段和官方 RGA 错误说明。程序不得在失败后继续写出
看似有效的输出文件。

## 10. 验证方案

### 10.1 静态检查

- 校验官方依赖的提交、API 版本、架构和许可证；
- 用 `file` 确认程序和动态库都是 aarch64；
- 用 `readelf`/`ldd` 确认动态库解析路径；
- 测试脚本从 debugfs 或 procfs 记录板端驱动版本，并确认仍为 v1.3.7。

### 10.2 负向测试

- 无参数运行必须失败；
- 输入路径不存在必须失败；
- 输入文件过短或过长必须失败；
- 不得留下被误认为成功结果的输出文件。

### 10.3 正向测试

使用此前 RKISP 采集的 1920x1080 NV12 帧：

- 程序退出状态为 0；
- 输出文件大小严格等于 1,382,400 字节；
- 输出 SHA-256 可重复记录；
- Y、UV 平面不是全零或单一填充值；
- 连续多次运行成功；
- 内核日志没有新增 RGA fault、MMU fault 或 ioctl 错误；
- 输出耗时统计完整。

必要时使用 RGA debugfs 统计或系统调用跟踪补充证明任务确实提交给 `/dev/rga`，
而不是仅凭生成了一个文件就宣称硬件加速成功。

## 11. 后续衔接

本实验验收后，下一份独立设计将把数据源替换为 V4L2/RKISP 缓冲区：

```text
OV13850 -> CIF -> RKISP -> V4L2 DMA-BUF -> RGA -> 目标缓冲区
```

届时重点学习 DMA-BUF fd 导入、缓存同步、队列生命周期以及端到端延迟。
文件实验的核心 RGA 调用和错误处理可以复用，但文件 I/O 不进入实时路径。

## 12. 参考资料

- 官方 librga 仓库：<https://github.com/airockchip/librga>
- 官方 RGA 开发指南：<https://github.com/airockchip/librga/blob/main/docs/Rockchip_Developer_Guide_RGA_CN.md>
- 官方 IM2D API 指南：<https://github.com/airockchip/librga/blob/main/docs/Rockchip_Developer_Guide_RGA_IM2D_CN.md>
