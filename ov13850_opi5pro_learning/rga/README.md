# Orange Pi 5 Pro RGA NV12 Resize

这个目录用于学习 RK3588S 的 RGA 用户态接口。当前最小实验读取一帧
1920x1080 NV12，通过 Rockchip 官方 librga/IM2D 缩放为 1280x720 NV12。

它验证的是文件到文件的 RGA 最小闭环，不是最终实时摄像头程序。实时版本还
需要处理 V4L2 队列、缓冲区生命周期，以及后续低延迟阶段的 DMA-BUF 和缓存
同步。

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
- 黑盒测试已完成 RED 验证，能发现缺少目标程序。

尚未宣称完成：

- Orange Pi 5 Pro 上的实际 `imresize()`；
- 输出 NV12 的板端内容检查；
- RGA/MMU/IOMMU fault 检查；
- V4L2 实时帧接入 RGA；
- DMA-BUF 零拷贝和端到端延迟。
