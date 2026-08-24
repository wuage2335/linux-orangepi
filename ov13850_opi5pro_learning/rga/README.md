# Orange Pi 5 Pro RGA NV12 Resize

这个目录用于学习 RK3588S 的 RGA 用户态接口。当前最小实验读取一帧
1920x1080 NV12，通过 Rockchip 官方 librga/IM2D 缩放为 1280x720 NV12。

目录同时包含文件隔离实验和第一版 V4L2 实时 copy path。实时版处理 V4L2
队列和缓冲区生命周期，但仍保留一次显式 memcpy；后续低延迟阶段再验证直接
导入 MMAP 地址、DMA-BUF 和缓存同步。

## Data Flow

```text
RKISP capture file: 1920x1080 NV12
                 |
                 v
        process virtual memory
                 |
                 v
 importbuffer_virtualaddr + wrapbuffer_handle
                 |
                 v
        synchronous RGA imresize
                 |
                 v
       output file: 1280x720 NV12
```

文件 I/O 位于计时区间之外。程序先执行 5 次预热，再统计 100 次同步
`imresize()` 的总耗时、平均耗时和推算吞吐。

## Official Dependency

项目固定使用 Rockchip 官方 librga：

- Repository: <https://github.com/airockchip/librga>
- Commit: `2b32edcb97b601b25683e2941d888c8515da6d55`
- RGA API: `1.10.6_[3]`
- Library: official `gcc-aarch64/librga.so`
- License: Apache License 2.0

来源和 SHA-256 记录在 `third_party/librga/ORIGIN.md`。动态库随部署包携带，
不会安装到 WSL 或板端的 `/usr`、`/usr/local`。

## Input And Output

| Item | Format | Resolution | Size |
| --- | --- | --- | ---: |
| Input | NV12 | 1920x1080 | 3,110,400 bytes |
| Output | NV12 | 1280x720 | 1,382,400 bytes |

第一版故意固定格式和分辨率，以便把 librga、RGA 驱动、NV12 内存布局和缩放
能力与通用参数解析隔离开。

## Build In WSL

需要安装 aarch64 C++ 交叉编译器：

```bash
sudo apt install g++-aarch64-linux-gnu
```

在 RGA 子目录执行：

```bash
cd ov13850_opi5pro_learning/rga
make clean bundle
```

也可以从仓库任意目录执行：

```bash
make -C ov13850_opi5pro_learning/rga clean bundle
```

生成的部署目录：

```text
build/rga_nv12_resize/
├── bin/rga_nv12_resize
├── bin/rga_v4l2_live
└── lib/librga.so
```

可执行文件的 RUNPATH 是 `$ORIGIN/../lib`，因此它会加载同一部署包中的
`lib/librga.so`，而不是依赖板端可能残留的其他版本。

## Run On Orange Pi 5 Pro

先确认板端 RGA 设备和驱动：

```bash
test -c /dev/rga

sudo cat /sys/kernel/debug/rkrga/driver_version 2>/dev/null ||
    cat /proc/rkrga/driver_version
```

当前板端历史实测驱动为：

```text
RGA multicore Device Driver: v1.3.7
```

运行缩放：

```bash
./bin/rga_nv12_resize \
    input-1920x1080.nv12 \
    output-1280x720.nv12
```

成功时最后一行应为：

```text
RGA_RESIZE_OK
```

并检查输出：

```bash
stat -c '%s %n' output-1280x720.nv12
sha256sum output-1280x720.nv12
```

期望大小是 `1,382,400` 字节。

## Real-Time V4L2 Copy Path

先使用已有脚本配置 OV13850、crop 和 RKISP mainpath：

```bash
./configure_rkisp_1080p.sh
```

然后运行实时实验：

```bash
./bin/rga_v4l2_live \
    /dev/video11 \
    /tmp/rga-live-last-1280x720.nv12
```

固定处理流程：

```text
丢弃 3 个启动帧
        ↓
V4L2 DQBUF 1920x1080 NV12
        ↓
memcpy 3,110,400 bytes 到复用源内存
        ↓
立即 QBUF capture buffer
        ↓
同步 RGA imresize 到 1280x720 NV12
        ↓
重复 300 帧并保存最后一帧
```

程序分别报告：

- `copy_average_us`：每帧显式 memcpy 平均耗时；
- `rga_average_us`：每帧同步 RGA resize 平均耗时；
- `capture_process_fps`：包含等待帧、copy、队列 ioctl 和 RGA 的循环吞吐；
- `dropped`：根据 V4L2 sequence 计算的跳帧数量；
- `timeouts`：2 秒 poll 超时数量。

copy 完成后立即 QBUF，因此 RGA 不再访问 V4L2 MMAP 地址。该版本不是
零拷贝，目的是先把 capture queue 与 RGA 生命周期分离验证。

板端验收：

```bash
bash tests/test_rga_v4l2_live.sh \
    build/rga_nv12_resize \
    /dev/video11 \
    /tmp/rga-live-last-1280x720.nv12
```

成功时必须处理 300 帧、timeout/drop 为 0，最后一行为
`RGA_V4L2_LIVE_OK`。

## Board Acceptance Test

测试脚本必须在 aarch64 Orange Pi 5 Pro 上执行，因为 WSL x86_64 不能运行
目标程序，也没有板端 `/dev/rga`。

```bash
bash tests/test_rga_nv12_resize.sh \
    build/rga_nv12_resize \
    input-1920x1080.nv12
```

测试覆盖：

- 程序和动态库的 aarch64 架构；
- bundle 内 librga 的实际加载路径；
- 错误参数、缺失输入、短输入和同路径保护；
- 两次输出大小、内容和确定性；
- librga 版本、预热次数、计时结果和成功标记。

## Current Status

已经验证：

- 官方 librga 来源、版本、架构和 SHA-256；
- C++ 源码的 aarch64 语法和完整链接；
- `make clean bundle` 可重复生成部署包；
- 可执行文件 RUNPATH 为 `$ORIGIN/../lib`；
- 文件式 RGA 黑盒测试已在板端转为 GREEN，实际 imresize、输出内容、性能和
  fault 检查均通过；详细证据见
  `docs/codex/rga_nv12_file_resize_validation.md`；
- 实时程序及其测试已完成 WSL aarch64 构建/语法验证，板端 300 帧验收尚未
  执行。

尚未宣称完成：

- V4L2 实时帧接入 RGA；
- DMA-BUF 零拷贝和端到端延迟。
