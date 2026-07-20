# Linux Orange Pi 源码功能索引

> 生成基准：Linux 6.1.99 源码树；扫描日期：2026-07-12。
> 本文件是**语义入口索引**：先定位子系统、入口和文档，再用 `rg` 精确查找具体文件或符号。当前源码约有 87,133 个文件，完整逐文件列表既难阅读也容易随构建产物过期。

## 快速检索

在源码根目录执行下列命令：

```sh
# 按文件名（最快；会包含所有受版本控制与未忽略文件）
rg --files | rg '关键词'

# 按函数、结构体、CONFIG 或设备树 compatible 查内容
rg -n '关键词' 路径

# 查某个 CONFIG 的定义和被使用位置
rg -n 'CONFIG_FOO|config FOO' .

# 查驱动的设备树匹配表和 probe 函数
rg -n 'of_match|compatible|probe' drivers/ arch/arm64/boot/dts/rockchip/

# 了解文件的改动背景（不修改工作树）
git log --oneline -- path/to/file
git blame path/to/file
```

常用的限定搜索：

```sh
rg --files drivers | rg 'rockchip|rk35|gpio|i2c'
rg -n 'rk3588s-orangepi-5' arch/arm64/boot/dts/rockchip/
rg -n '^config |^menuconfig ' drivers/*/Kconfig
```

## 总体结构

| 路径 | 职责 | 优先入口 / 说明 |
| --- | --- | --- |
| `init/` | 内核启动与初始化调用 | `init/main.c`：`start_kernel()`、`mm_init()`、`do_initcalls()` |
| `arch/` | 各 CPU 架构相关实现 | 此工程重点为 `arch/arm64/` |
| `kernel/` | 调度、定时器、中断、锁、trace、BPF 等核心机制 | `kernel/sched/core.c`：`sched_init()` |
| `mm/` | 虚拟内存、页分配、SLAB、页缓存、OOM | 从 `mm/memory.c`、`mm/mmap.c`、`mm/page_alloc.c` 入手 |
| `fs/` | VFS 与各类文件系统 | `fs/namei.c`、`fs/open.c`、`fs/read_write.c`、`fs/dcache.c` |
| `block/` | 块层、bio/request 与 I/O 调度 | `block/blk-core.c`、`block/blk-mq.c` |
| `drivers/` | 设备驱动及驱动模型 | 先读 `drivers/base/` 与 `drivers/Makefile` |
| `net/` | 网络协议栈、socket、netfilter | `net/socket.c`、`net/ipv4/`、`net/ipv6/`、`net/netfilter/` |
| `ipc/` | System V IPC 与 POSIX 消息队列 | `ipc/` |
| `io_uring/` | 异步 I/O 框架 | `io_uring/io_uring.c` |
| `security/` | LSM、安全模块、密钥管理 | `security/security.c`、`security/selinux/` |
| `crypto/` | 内核密码算法与接口 | `crypto/` |
| `sound/` | ALSA 与音频驱动 | `sound/core/`、`sound/soc/` |
| `virt/` | 虚拟化基础设施 | `virt/` |
| `include/` | 内核内部与 UAPI 头文件 | `include/linux/` 为内部接口；`include/uapi/` 为用户态 ABI |
| `Documentation/` | 官方内核开发与子系统文档 | 从 `Documentation/index.rst` 开始 |
| `tools/`、`samples/` | 开发、测试与示例程序 | eBPF、perf、内存模型及示例 |

## 启动、配置与构建

| 目标 | 文件 / 路径 | 要点 |
| --- | --- | --- |
| 总构建入口 | `Makefile` | 选择架构 Makefile、Kconfig 和 `vmlinux` 构建规则 |
| 配置入口 | `Kconfig` | 依次包含 init、kernel、mm、net、drivers、fs、security 等 Kconfig |
| ARM64 构建 | `arch/arm64/Makefile` | ARM64 编译选项及 `arch/arm64/boot/Image` 目标 |
| ARM64 最早汇编 | `arch/arm64/kernel/head.S` | `primary_entry`，建立早期执行环境后进入 C 代码 |
| C 语言启动入口 | `init/main.c` | `start_kernel()` 位于约第 939 行 |
| initcall 执行 | `init/main.c` | `do_initcalls()`；驱动的 `module_init()` 最终由此体系调度 |
| 配置语言 | `Documentation/kbuild/kconfig-language.rst` | `Kconfig` 语法与依赖关系 |
| 构建系统 | `Documentation/kbuild/` | Kbuild、模块和编译数据库相关资料 |

启动主线：`head.S: primary_entry` → `start_kernel()` → 内存/中断/调度初始化 → `do_initcalls()` → 设备驱动 probe → 用户空间 init。

## ARM64 与 Orange Pi / Rockchip

| 主题 | 路径 | 快速定位 |
| --- | --- | --- |
| ARM64 架构代码 | `arch/arm64/` | `kernel/`、`mm/`、`include/`、`boot/` 是最常用子目录 |
| 设备树总目录 | `arch/arm64/boot/dts/rockchip/` | 板级 `.dts`、共用 `.dtsi` 和 SoC `.dtsi` |
| Orange Pi 5 | `rk3588s-orangepi-5.dts` | 及 `rk3588s-orangepi-5.dtsi` |
| Orange Pi 5B / 5 Pro | `rk3588s-orangepi-5b.dts`、`rk3588s-orangepi-5-pro.dts` | 对比同 SoC 板级差异 |
| Orange Pi 5 Plus / Max / Ultra | `rk3588-orangepi-5-plus.dts`、`rk3588-orangepi-5-max.dts`、`rk3588-orangepi-5-ultra.dts` | RK3588 板级描述 |
| Orange Pi CM5 | `rk3588s-orangepi-cm5.dts` | 另有 tablet/camera 变体 |
| Orange Pi RK3399 | `rk3399-orangepi.dts` | 较早的 Rockchip 平台 |
| Rockchip 默认配置 | `arch/arm64/configs/rockchip_linux_defconfig` | BSP 基础配置；另有 `rk3588_linux.config` 等分片 |
| Rockchip 时钟/SoC | `drivers/clk/rockchip/`、`drivers/soc/rockchip/` | 平台时钟、SoC 支持 |
| Rockchip 设备支持 | `drivers/{phy,pinctrl,gpu,media,thermal,reset}/rockchip/` | 以实际硬件类别选择目录 |
| BSP 扩展 | `drivers/rknpu/`、`drivers/rkflash/`、`drivers/rk_nand/` | Rockchip 专用 NPU、存储相关实现 |

阅读板级支持时，始终按这个方向跟踪：`.dts` 节点的 `compatible` → 驱动的 `of_device_id` 匹配表 → `probe()` → 子系统 API。

## 核心功能入口

| 想理解的功能 | 首读文件 | 接下来查看 |
| --- | --- | --- |
| 进程与调度 | `kernel/sched/core.c` | `kernel/fork.c`、`kernel/exit.c`、`kernel/sched/` |
| 系统调用 | `arch/arm64/kernel/` | `kernel/sys.c`、`fs/open.c`、`fs/read_write.c` |
| 中断 | `kernel/irq/` | `drivers/irqchip/`、`Documentation/core-api/irq/` |
| 内存映射与缺页 | `mm/memory.c` | `mm/mmap.c`、`mm/page_alloc.c`、`mm/slub.c` |
| 文件访问 | `fs/namei.c` | `fs/open.c`、`fs/read_write.c`、`fs/ext4/` |
| 块 I/O | `block/blk-core.c` | `block/blk-mq.c`、`drivers/mmc/`、`drivers/nvme/` |
| socket 与 TCP/IP | `net/socket.c` | `net/core/`、`net/ipv4/`、`net/ipv6/` |
| 防火墙 | `net/netfilter/` | `include/uapi/linux/netfilter*` |
| 驱动模型 | `drivers/base/` | `drivers/of/`、`drivers/Makefile` |
| GPIO / I²C / SPI | `drivers/gpio/` | `drivers/i2c/`、`drivers/spi/`、`drivers/pinctrl/` |
| 电源管理 | `drivers/cpufreq/` | `drivers/cpuidle/`、`drivers/thermal/`、`Documentation/admin-guide/pm/` |
| 追踪与调试 | `kernel/trace/` | `Documentation/trace/`、`tools/perf/` |

## 文档优先级

1. `Documentation/admin-guide/README.rst`：源码树官方建议的起点。
2. `Documentation/arm64/booting.rst`：ARM64 内核映像与启动约定。
3. `Documentation/core-api/index.rst`：内核关键通用 API。
4. `Documentation/driver-api/index.rst`：写和读驱动所需接口。
5. `Documentation/devicetree/`：设备树规范及绑定文档。
6. `Documentation/locking/`：并发与锁。
7. `Documentation/trace/`：ftrace、tracepoint 与事件追踪。

## 文件类型约定

- `Kconfig`：功能开关、依赖与菜单；先查它确定代码是否会参与构建。
- `Makefile`：对象如何被编进内核或模块；`obj-y` 是内建，`obj-m` 是模块。
- `.dts` / `.dtsi`：硬件描述；`.dts` 通常为板级顶层，`.dtsi` 为复用片段。
- `include/linux/`：内核内部接口；`include/uapi/`：不得随意破坏的用户态 ABI。
- `Documentation/devicetree/bindings/`：设备树属性与 `compatible` 的权威定义。

## 工作树状态

索引建立时，工作树已有 netfilter 与 memory-model 相关的未提交修改。阅读、构建或实验前先用 `git status --short` 确认状态；不要用清理命令覆盖这些改动。
