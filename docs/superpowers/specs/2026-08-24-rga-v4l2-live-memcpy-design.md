# Orange Pi 5 Pro V4L2 Real-Time RGA Copy Path Design

日期：2026-08-24

## 1. 背景

阶段 3 已完成两个基线：

1. OV13850 RAW10 经 CIF/RKISP 稳定输出 1920x1080 NV12，约 30 fps；
2. 官方 librga 1.10.6_[3] 在板端把 1920x1080 NV12 文件缩放为
   1280x720 NV12，纯 RGA resize 约 1.63-2.45 ms/次。

文件实验没有覆盖 V4L2 buffer 的申请、出队、归还和连续帧处理。本设计建立
第一版实时路径，并故意保留一次显式 memcpy，使 V4L2 队列与 RGA buffer 生命周期
相互独立，便于学习和定位问题。

## 2. 目标

新增一个板端程序，固定处理 300 帧实时 RKISP NV12：

```text
/dev/video11 V4L2 MMAP
        -> memcpy 到可复用源内存
        -> 立即归还 V4L2 capture buffer
        -> RGA imresize
        -> 1280x720 NV12
```

程序预丢弃 3 帧，正式处理 300 帧，保存第 300 帧的 RGA 输出，并分别报告
capture loop、memcpy 和 RGA 的性能数据。

## 3. 非目标

第一版不包含：

- 不直接把 V4L2 MMAP 地址导入 RGA；
- 不使用 `VIDIOC_EXPBUF` 或 DMA-BUF fd；
- 不实现多线程 capture/process pipeline；
- 不修改 sensor、subdev、crop 或 mainpath 格式；
- 不显示图像，不调用 MPP，不编码或推流；
- 不实现任意输入输出尺寸或格式；
- 不把纯处理耗时表述为端到端摄像头延迟。

直接导入 MMAP 地址和 DMA-BUF 分别作为后续优化设计。

## 4. 命令行与固定数据契约

程序接口：

```text
rga_v4l2_live <video-device> <last-output-1280x720.nv12>
```

示例：

```bash
rga_v4l2_live /dev/video11 /tmp/rga-live-last-1280x720.nv12
```

固定参数：

- V4L2 buffer type: `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`；
- V4L2 memory: `V4L2_MEMORY_MMAP`；
- MMAP buffer 数量：4；
- 输入：1920x1080 `V4L2_PIX_FMT_NV12`；
- 输入 plane 数量：1；
- 输入 bytesperline：1920；
- 输入有效数据：至少 3,110,400 bytes，每帧只复制前 3,110,400 bytes；
- 输出：1280x720 `RK_FORMAT_YCbCr_420_SP`；
- 输出大小：1,382,400 bytes；
- 预丢弃：3 帧；
- 正式处理：300 帧；
- poll timeout：2000 ms，首次超时即失败。

程序只读取 `VIDIOC_G_FMT` 并严格校验。运行前由现有
`configure_rkisp_1080p.sh` 建立传感器、crop 和 RKISP mainpath 格式。

## 5. 组件边界

### 5.1 VideoCapture

负责 V4L2 fd 和 capture 队列：

- `open(O_RDWR | O_NONBLOCK)`；
- `VIDIOC_QUERYCAP` 验证 multiplanar capture 与 streaming；
- `VIDIOC_G_FMT` 验证固定 NV12 数据契约；
- `VIDIOC_REQBUFS` 申请 4 个 MMAP buffers；
- 对每个 index 执行 `VIDIOC_QUERYBUF` 和 `mmap`；
- 启动前 `VIDIOC_QBUF` 全部 buffers；
- `VIDIOC_STREAMON` 后通过 `poll` + `VIDIOC_DQBUF` 获取帧；
- 每次复制完成后立即 `VIDIOC_QBUF`；
- 析构或失败清理执行 `VIDIOC_STREAMOFF`、`munmap` 和 `close`。

`VideoCapture` 不理解 RGA，也不保存输出文件。

### 5.2 RgaResizer

复用文件实验已经验证的 RGA 模式：

- 分配一个 3,110,400-byte `src_data`；
- 分配一个 1,382,400-byte `dst_data`；
- 两块内存各执行一次 `importbuffer_virtualaddr`；
- 用 `wrapbuffer_handle` 描述固定 NV12 宽高；
- 构造时执行一次 `imcheck`；
- 每帧调用同步 `imresize`；
- 析构释放两个 RGA handles。

`RgaResizer` 不打开 V4L2 device，也不控制 capture queue。

### 5.3 Main Loop

主流程只编排两个组件：

1. 启动 capture；
2. DQBUF/QBUF 丢弃 3 帧；
3. 开始完整循环计时；
4. `poll` 并 DQBUF；
5. 检查 plane 0 的 `bytesused >= 3,110,400`；
6. 计时并 memcpy 到 `RgaResizer::src_data`；
7. 立即 QBUF 当前 capture buffer；
8. 计时并执行同步 RGA resize；
9. 更新 sequence、帧数和耗时统计；
10. 重复至 300 帧；
11. 结束完整循环计时；
12. 把最后一次 `dst_data` 写入目标文件；
13. 打印统计与 `RGA_V4L2_LIVE_OK`。

归还 capture buffer 发生在 RGA 之前，因为 memcpy 后 RGA 不再访问 V4L2
buffer。这是显式 copy path 的主要业务边界。

## 6. V4L2 Buffer 生命周期

每个 MMAP buffer 的状态循环：

```text
MMAP/owned by userspace
        -> QBUF
queued/owned by driver
        -> sensor + ISP fill
done/owned by driver
        -> DQBUF
owned by userspace
        -> memcpy
        -> QBUF
```

用户态只在 DQBUF 后访问地址，并在 QBUF 前完成 memcpy。QBUF 后不得继续读取
该地址，因为驱动可以立即覆盖它。

## 7. Sequence 与性能统计

正式处理的第一帧只建立 `previous_sequence`。从第二帧开始：

```text
sequence_gap = current_sequence - previous_sequence - 1
dropped_frames += sequence_gap
```

使用无符号差值处理 sequence 回绕。成功标准要求 `dropped_frames=0`。

使用 `std::chrono::steady_clock` 统计：

- `copy_total_us` / 300：单次 3,110,400-byte memcpy 平均耗时；
- `rga_total_us` / 300：单次同步 imresize 平均耗时；
- `loop_total_s`：从正式处理开始到第 300 帧 RGA 完成；
- `capture_process_fps = 300 / loop_total_s`。

loop FPS 包含等待帧、memcpy、QBUF/DQBUF 和 RGA，但不包含预丢弃、初始化和
最后写盘。它是本程序的实时处理吞吐，仍不包含显示、MPP 或网络。

## 8. 错误处理与清理

以下情况返回非零并打印 `ERROR:`：

- 参数数量错误；
- video device 无法打开；
- capability 不支持 multiplanar capture 或 streaming；
- 当前格式不是固定的 1920x1080 NV12 单平面；
- REQBUFS 返回少于 2 个 buffers；
- QUERYBUF、mmap、QBUF、STREAMON、poll 或 DQBUF 失败；
- poll 超时；
- plane index、length 或 bytesused 不满足输入大小；
- RGA import、imcheck 或 imresize 失败；
- 输出文件无法完整写入 1,382,400 bytes。

清理必须幂等。若 stream 已启动，先 STREAMOFF；随后 munmap 所有成功映射的
buffers、释放 RGA handles、关闭 fd。输出文件在开始前删除，失败时不能留下旧
结果或残缺结果。

## 9. 文件结构

```text
ov13850_opi5pro_learning/rga/
├── src/
│   ├── rga_nv12_resize.cpp
│   └── rga_v4l2_live.cpp
├── tests/
│   ├── test_rga_nv12_resize.sh
│   └── test_rga_v4l2_live.sh
├── Makefile
└── README.md
```

文件程序保持不变，继续作为 RGA 隔离基线。Makefile 的 bundle 同时携带两个
可执行文件和一个官方 `librga.so`。

## 10. 验证方案

### 10.1 WSL 静态验证

- 测试脚本 `bash -n`；
- 两个 C++ 程序均使用 `-Wall -Wextra -Werror` 交叉构建；
- `file` 验证 aarch64；
- `readelf` 验证两个程序 RUNPATH 均为 `$ORIGIN/../lib`；
- bundle 中 librga SHA-256 与官方固定值一致；
- clean build 后 Git 状态不包含 `build/`。

### 10.2 板端负向验证

- 无参数运行返回非零并输出 `ERROR:`；
- `/dev/null` 作为 device 返回非零并输出 `ERROR:`；
- 格式不匹配时在 STREAMON 前拒绝；
- 失败后不存在目标输出文件。

### 10.3 板端正向验证

运行现有 RKISP 配置脚本后：

- 处理成功帧数为 300；
- pre-skipped 为 3；
- timeout 为 0；
- sequence drop 为 0；
- loop FPS 约为 30；
- copy/RGA 平均耗时字段存在；
- 最后一帧文件严格为 1,382,400 bytes；
- Y/UV 平面大小和数值范围有效；
- 最后一行是 `RGA_V4L2_LIVE_OK`；
- 程序退出后 sensor runtime PM 为 suspended、usage 0；
- 没有新增 CIF/ISP/RGA/MMU/IOMMU fault、timeout 或 overflow。

## 11. 后续衔接

本版通过后，以相同 300 帧契约实现“直接导入 V4L2 MMAP buffer”版本，比较
显式 memcpy 路径与直接导入路径的 CPU 占用和处理时延。DMA-BUF fd 路径继续
留给低延迟优化阶段，不与本版混合实现。
