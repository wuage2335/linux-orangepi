# V4L2 Real-Time RGA Copy Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Do not dispatch subagents; the user requires inline execution.

**Goal:** 构建一个在 Orange Pi 5 Pro 上从 RKISP mainpath 连续取得 300 帧 1920x1080 NV12，经显式 memcpy 后使用 RGA 缩放为 1280x720 NV12 的实时实验。

**Architecture:** 使用 4 个 V4L2 multiplanar MMAP capture buffers；每帧 DQBUF 后复制到一个长期复用的 RGA 源缓冲区，并立即 QBUF，再同步执行 RGA resize。程序预丢弃 3 帧、处理 300 帧、保存最后输出，并独立统计 copy、RGA 和完整循环吞吐。

**Tech Stack:** Linux V4L2 ioctl/poll/mmap、C++17、Rockchip librga IM2D 1.10.6_[3]、GNU Make、Bash、aarch64-linux-gnu 交叉工具链。

---

## 文件结构

- Create: `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp`
- Create: `ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh`
- Modify: `ov13850_opi5pro_learning/rga/Makefile`
- Modify: `ov13850_opi5pro_learning/rga/README.md`
- Create after board validation: `docs/codex/rga_v4l2_live_validation.md`
- Modify after board validation: `docs/codex/HANDOFF.md`

已有 `rga_nv12_resize.cpp` 和测试保持不变。

### Task 1: 写实时程序黑盒测试并验证 RED

**Files:**
- Create: `ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh`

- [ ] **Step 1: 创建测试脚本**

脚本接口固定为：

```bash
test_rga_v4l2_live.sh <bundle-dir> <video-device> <output-file>
```

测试必须包含以下真实行为：

```bash
expect_failure no_args
expect_failure invalid_device /dev/null "$TEST_ROOT/invalid.nv12"

"$BIN" "$DEVICE" "$OUTPUT" | tee "$TEST_ROOT/live.log"

[[ $(stat -c '%s' "$OUTPUT") -eq 1382400 ]]
grep -Fq 'pre_skipped=3' "$TEST_ROOT/live.log"
grep -Fq 'processed=300' "$TEST_ROOT/live.log"
grep -Fq 'timeouts=0' "$TEST_ROOT/live.log"
grep -Fq 'dropped=0' "$TEST_ROOT/live.log"
grep -Eq 'copy_average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log"
grep -Eq 'rga_average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log"
grep -Eq 'capture_process_fps=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log"
grep -Fq 'RGA_V4L2_LIVE_OK' "$TEST_ROOT/live.log"
```

脚本还要验证目标程序和 librga 都是 aarch64，并使用 `ldd` + `readlink -f`
证明程序加载 bundle 内动态库。输出文件必须非全零。

- [ ] **Step 2: 验证脚本语法**

Run:

```bash
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh
```

Expected: 无输出，退出状态 0。

- [ ] **Step 3: 观察 RED**

Run:

```bash
mkdir -p /tmp/rga-live-empty-bundle
bash ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh \
    /tmp/rga-live-empty-bundle /dev/null /tmp/rga-live-red.nv12
```

Expected: `FAIL: missing executable`，非零退出。

- [ ] **Step 4: 提交测试**

```bash
git add ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh
git commit -m "test(rga): define realtime v4l2 copy-path acceptance"
```

### Task 2: 实现 V4L2 capture 与 RGA 实时循环

**Files:**
- Create: `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp`

- [ ] **Step 1: 定义固定数据契约与系统调用包装**

源码定义：

```cpp
constexpr int kSrcWidth = 1920;
constexpr int kSrcHeight = 1080;
constexpr int kDstWidth = 1280;
constexpr int kDstHeight = 720;
constexpr std::size_t kSrcSize = 3110400;
constexpr std::size_t kDstSize = 1382400;
constexpr unsigned int kRequestedBuffers = 4;
constexpr unsigned int kSkipFrames = 3;
constexpr unsigned int kProcessFrames = 300;
constexpr int kPollTimeoutMs = 2000;
```

`xioctl()` 必须在 `errno == EINTR` 时重试。所有系统调用错误使用
`std::runtime_error` 携带操作名和 `strerror(errno)`。

- [ ] **Step 2: 实现 VideoCapture 所有权边界**

类接口固定为：

```cpp
struct CapturedFrame {
    unsigned int index;
    unsigned char *data;
    std::size_t length;
    std::size_t bytes_used;
    std::uint32_t sequence;
};

class VideoCapture {
public:
    explicit VideoCapture(const char *device);
    ~VideoCapture();
    void start();
    void stop();
    CapturedFrame dequeue(unsigned int &timeouts);
    void requeue(unsigned int index);
};
```

构造函数完成 QUERYCAP、G_FMT、REQBUFS、QUERYBUF 和 mmap。构造失败时必须
调用内部 `cleanup()`，因为未完成构造的对象不会执行析构函数。析构顺序固定为
STREAMOFF、munmap、close。

- [ ] **Step 3: 实现 RgaResizer**

类接口固定为：

```cpp
class RgaResizer {
public:
    RgaResizer();
    unsigned char *source_data();
    const std::vector<unsigned char> &output_data() const;
    void resize();
};
```

构造时分配源/目标 vector、各导入一次 handle、wrap 固定 NV12，并执行一次
`imcheck`。`resize()` 调用同步 `imresize`，失败时抛出包含 `imStrError()` 的
异常。handle 使用不可复制 RAII 类自动释放。

- [ ] **Step 4: 实现 3+300 帧主循环**

每个正式帧严格执行：

```cpp
CapturedFrame frame = capture.dequeue(timeouts);

auto copy_start = steady_clock::now();
std::memcpy(resizer.source_data(), frame.data, kSrcSize);
auto copy_end = steady_clock::now();

capture.requeue(frame.index);

auto rga_start = steady_clock::now();
resizer.resize();
auto rga_end = steady_clock::now();
```

前 3 帧只 DQBUF/QBUF，不进入统计。正式循环前启动 loop timer，第 300 帧 RGA
结束后停止 timer。sequence delta 为 0 记 1 个异常；delta 大于 1 时累计
`delta - 1`。完成循环后先 `capture.stop()`，再写最后输出。

- [ ] **Step 5: 固定成功输出**

成功日志必须包含：

```text
input=1920x1080 NV12 bytes=3110400
output=1280x720 NV12 bytes=1382400
pre_skipped=3 processed=300 timeouts=0 dropped=0
copy_total_us=... copy_average_us=...
rga_total_us=... rga_average_us=...
loop_total_s=... capture_process_fps=...
RGA_V4L2_LIVE_OK
```

输出文件写入失败时删除残缺文件。任何异常由 `main()` 转成 `ERROR:` 和非零
退出码。

- [ ] **Step 6: 交叉语法与链接验证**

```bash
aarch64-linux-gnu-g++ \
    -std=gnu++17 -Wall -Wextra -Werror \
    -Iov13850_opi5pro_learning/rga/third_party/librga/include \
    -fsyntax-only \
    ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp
```

然后完整链接到 `/tmp/rga_v4l2_live-link-check`，Expected: ARM aarch64 ELF，
NEEDED 包含 `librga.so`。

- [ ] **Step 7: 提交实现**

```bash
git add ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp
git commit -m "feat(rga): add realtime v4l2 copy path"
```

### Task 3: 扩展 bundle 和 README

**Files:**
- Modify: `ov13850_opi5pro_learning/rga/Makefile`
- Modify: `ov13850_opi5pro_learning/rga/README.md`

- [ ] **Step 1: Makefile 生成第二个程序**

增加：

```make
FILE_SOURCE := $(ROOT)/src/rga_nv12_resize.cpp
LIVE_SOURCE := $(ROOT)/src/rga_v4l2_live.cpp
FILE_BIN := $(BUILD)/bin/rga_nv12_resize
LIVE_BIN := $(BUILD)/bin/rga_v4l2_live

bundle: $(FILE_BIN) $(LIVE_BIN) $(LIB)
```

两个目标使用相同头文件、librga 和 `$ORIGIN/../lib` RUNPATH，但分别依赖各自
SOURCE，防止修改一个源文件时无条件重建另一个程序。

- [ ] **Step 2: README 增加实时 copy path**

记录命令：

```bash
./bin/rga_v4l2_live \
    /dev/video11 \
    /tmp/rga-live-last-1280x720.nv12
```

明确先运行 `configure_rkisp_1080p.sh`，并解释
`DQBUF -> memcpy -> QBUF -> RGA` 与未来 direct-MMAP/DMA-BUF 的区别。

- [ ] **Step 3: 从零构建并验证 bundle**

```bash
make -C ov13850_opi5pro_learning/rga clean bundle
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh
file ov13850_opi5pro_learning/rga/build/rga_nv12_resize/bin/*
readelf -d ov13850_opi5pro_learning/rga/build/rga_nv12_resize/bin/rga_v4l2_live
git diff --check
```

Expected: 两个 aarch64 ELF，RUNPATH 均为 `$ORIGIN/../lib`，build 目录被忽略。

- [ ] **Step 4: 提交构建和说明**

```bash
git add \
    ov13850_opi5pro_learning/rga/Makefile \
    ov13850_opi5pro_learning/rga/README.md
git commit -m "build(rga): package realtime v4l2 experiment"
```

### Task 4: 板端验收 300 帧

**Files:**
- Create after evidence: `docs/codex/rga_v4l2_live_validation.md`
- Modify after evidence: `docs/codex/HANDOFF.md`

- [ ] **Step 1: 打包并传输单个 tar.gz**

部署包包含两个 binaries、一个 librga、实时测试脚本和
`configure_rkisp_1080p.sh`。生成 SHA-256 后传输到当前板端地址并在板端复核。

- [ ] **Step 2: 记录板端运行前基线**

```bash
uname -r
cat /sys/class/video4linux/video11/name
v4l2-ctl -d /dev/video11 --get-fmt-video
sudo cat /sys/kernel/debug/rkrga/driver_version
```

Expected: current kernel、`rkisp_mainpath`、1920x1080 NV12、RGA driver v1.3.7。

- [ ] **Step 3: 运行负向和正向验收**

```bash
bash test_rga_v4l2_live.sh \
    rga-nv12-file-resize-bundle \
    /dev/video11 \
    /tmp/rga-live-last-1280x720.nv12
```

Expected: 300 frames、0 timeout、0 dropped、约 30 fps、`RGA_V4L2_LIVE_OK`、
测试最后 PASS。

- [ ] **Step 4: 输出、PM 与内核日志检查**

检查输出严格 1,382,400 bytes、Y/UV 范围有效；sensor PM 为 suspended/usage 0；
测试新增日志没有 CIF/ISP/RGA/MMU/IOMMU fault、timeout 或 overflow。

- [ ] **Step 5: 写实机证据并提交**

验证文档必须记录实际内核、输入输出、300 帧统计、copy/RGA/loop 性能、PM 和
内核日志。HANDOFF 下一步改为 direct-MMAP 对比设计。提交：

```bash
git add docs/codex/rga_v4l2_live_validation.md docs/codex/HANDOFF.md
git commit -m "docs: record realtime v4l2 rga validation"
```

### Task 5: 最终复核与 main 合并

- [ ] **Step 1: 最终构建和 Git 检查**

```bash
make -C ov13850_opi5pro_learning/rga clean bundle
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh
git diff --check
git status -sb
```

- [ ] **Step 2: 快进合并 main**

确认主目录只有既有 `docs/codex/task_plan.md` 修改后：

```bash
cd ~/linux-orangepi
git merge --ff-only codex/rga-nv12-file-resize
```

不自动推送。worktree 保留给下一项 direct-MMAP 对比实验。
