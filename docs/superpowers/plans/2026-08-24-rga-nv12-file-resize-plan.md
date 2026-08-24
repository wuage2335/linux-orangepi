# RGA NV12 File Resize Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Do not dispatch subagents; the user requires inline mentoring and review checkpoints.

**Goal:** 引入固定版本的 Rockchip 官方 librga，并构建一个在 Orange Pi 5 Pro 上把 1920x1080 NV12 文件缩放为 1280x720 NV12 的最小硬件实验。

**Architecture:** 官方 aarch64 头文件和动态库作为项目内第三方依赖保存，不安装到板端系统目录。C++ 程序一次导入源、目标虚拟地址缓冲区，通过 IM2D 同步执行 5 次预热和 100 次计时缩放；Shell 测试负责负向输入检查、动态库路径检查、输出尺寸与内容检查。实现和验证在独立 worktree 中逐项完成。

**Tech Stack:** C++17（GNU 扩展）、Rockchip librga/IM2D API 1.10.6、aarch64-linux-gnu 交叉工具链、GNU Make、Bash、V4L2、Orange Pi 5 Pro RK3588S。

---

## 文件结构

将创建以下文件：

- `ov13850_opi5pro_learning/rga/third_party/librga/ORIGIN.md`：固定上游仓库、提交、API 版本和文件校验值。
- `ov13850_opi5pro_learning/rga/third_party/librga/COPYING`：官方 Apache-2.0 许可证原文。
- `ov13850_opi5pro_learning/rga/third_party/librga/include/*`：固定提交中的官方公开头文件。
- `ov13850_opi5pro_learning/rga/third_party/librga/lib/aarch64/librga.so`：官方 gcc-aarch64 动态库。
- `ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh`：板端黑盒验收脚本。
- `ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp`：唯一的文件缩放程序。
- `ov13850_opi5pro_learning/rga/Makefile`：交叉编译及部署包生成入口。
- `ov13850_opi5pro_learning/rga/.gitignore`：排除本地 `build/` 部署产物。
- `ov13850_opi5pro_learning/rga/README.md`：学习目标、构建和运行说明。
- `docs/codex/rga_nv12_file_resize_validation.md`：实机验证证据。

只更新 `docs/codex/HANDOFF.md` 的当前进度；不覆盖当前工作树中已有的
`docs/codex/task_plan.md` 改动。

### Task 1: 建立隔离 worktree 并引入固定官方依赖

**Files:**
- Create: `ov13850_opi5pro_learning/rga/third_party/librga/ORIGIN.md`
- Create: `ov13850_opi5pro_learning/rga/third_party/librga/COPYING`
- Create: `ov13850_opi5pro_learning/rga/third_party/librga/include/*`
- Create: `ov13850_opi5pro_learning/rga/third_party/librga/lib/aarch64/librga.so`

- [ ] **Step 1: 确认主仓库状态并创建 worktree**

在主仓库执行：

```bash
cd ~/linux-orangepi
git status -sb
git check-ignore -q .worktrees
git worktree add .worktrees/rga-nv12-file-resize \
    -b codex/rga-nv12-file-resize main
cd .worktrees/rga-nv12-file-resize
git status -sb
```

Expected：新分支为 `codex/rga-nv12-file-resize`，worktree 干净；主仓库已有的
`docs/codex/task_plan.md` 改动不会出现在新 worktree 中。

- [ ] **Step 2: 检出并验证官方固定提交**

```bash
UPSTREAM=/tmp/librga-1.10.6-2b32edcb

if [ ! -d "$UPSTREAM/.git" ]; then
    git clone https://github.com/airockchip/librga.git "$UPSTREAM"
fi

git -C "$UPSTREAM" fetch origin main
git -C "$UPSTREAM" checkout --detach \
    2b32edcb97b601b25683e2941d888c8515da6d55

test "$(git -C "$UPSTREAM" rev-parse HEAD)" = \
    2b32edcb97b601b25683e2941d888c8515da6d55

sha256sum \
    "$UPSTREAM/libs/Linux/gcc-aarch64/librga.so" \
    "$UPSTREAM/COPYING"
```

Expected：

```text
e150bda757fb5e8a649c429ec7cabaf851aa2a3be554ed494d5519e8790d943b  .../librga.so
58f1fdcee3211f839f749c1ed97ca87fd56d9d01d729bb74241eca9e2ac710bc  .../COPYING
```

- [ ] **Step 3: 复制官方依赖到学习目录**

```bash
RGA_ROOT=ov13850_opi5pro_learning/rga
VENDOR="$RGA_ROOT/third_party/librga"

mkdir -p "$VENDOR/include" "$VENDOR/lib/aarch64"
cp -a "$UPSTREAM/include/." "$VENDOR/include/"
cp "$UPSTREAM/COPYING" "$VENDOR/COPYING"
cp "$UPSTREAM/libs/Linux/gcc-aarch64/librga.so" \
    "$VENDOR/lib/aarch64/librga.so"
chmod 0644 "$VENDOR/COPYING" "$VENDOR/lib/aarch64/librga.so"
find "$VENDOR/include" -type f -exec chmod 0644 {} +
```

- [ ] **Step 4: 写入来源清单**

创建 `ov13850_opi5pro_learning/rga/third_party/librga/ORIGIN.md`：

```markdown
# Official librga Origin

- Repository: https://github.com/airockchip/librga
- Commit: 2b32edcb97b601b25683e2941d888c8515da6d55
- RGA API: 1.10.6_[3]
- Headers: include/
- Shared library: libs/Linux/gcc-aarch64/librga.so
- Target: 64-bit Linux aarch64, including RK3588
- License: Apache License 2.0; see COPYING

## SHA-256

```text
e150bda757fb5e8a649c429ec7cabaf851aa2a3be554ed494d5519e8790d943b  lib/aarch64/librga.so
58f1fdcee3211f839f749c1ed97ca87fd56d9d01d729bb74241eca9e2ac710bc  COPYING
```
```

- [ ] **Step 5: 验证依赖内容与架构**

```bash
sha256sum "$VENDOR/lib/aarch64/librga.so" "$VENDOR/COPYING"
file "$VENDOR/lib/aarch64/librga.so"
grep -nE 'RGA_API_(MAJOR|MINOR|REVISION|BUILD)_VERSION' \
    "$VENDOR/include/im2d_version.h"
git diff --check
```

Expected：动态库为 `ELF 64-bit ... ARM aarch64`，版本宏为 `1`, `10`, `6`, `3`。

- [ ] **Step 6: 提交官方依赖**

```bash
git add ov13850_opi5pro_learning/rga/third_party/librga
git commit -m "build(rga): vendor official librga 1.10.6"
```

### Task 2: 先写板端黑盒测试

**Files:**
- Create: `ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh`

- [ ] **Step 1: 创建失败优先的测试脚本**

创建 `ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh`：

```bash
#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 2 ]] || {
    printf 'usage: %s <bundle-dir> <1920x1080-nv12>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
INPUT=$2
BIN="$BUNDLE/bin/rga_nv12_resize"
LIB="$BUNDLE/lib/librga.so"
TEST_ROOT=$(mktemp -d)

cleanup()
{
    rm -rf "$TEST_ROOT"
}
trap cleanup EXIT

fail()
{
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

expect_failure()
{
    local name=$1
    shift

    if "$BIN" "$@" >"$TEST_ROOT/$name.stdout" \
        2>"$TEST_ROOT/$name.stderr"; then
        fail "$name unexpectedly succeeded"
    fi

    grep -Fq 'ERROR:' "$TEST_ROOT/$name.stderr" ||
        fail "$name did not report ERROR:"
}

[[ -x $BIN ]] || fail "missing executable: $BIN"
[[ -f $LIB ]] || fail "missing bundled library: $LIB"
[[ -r $INPUT ]] || fail "input is not readable: $INPUT"
[[ $(stat -c '%s' "$INPUT") -eq 3110400 ]] ||
    fail 'input size is not 3110400 bytes'

file "$BIN" | grep -Fq 'ARM aarch64' || fail 'binary is not aarch64'
file "$LIB" | grep -Fq 'ARM aarch64' || fail 'librga is not aarch64'
ldd "$BIN" | grep -F "$LIB" >/dev/null ||
    fail 'binary did not resolve the bundled librga.so'

expect_failure no_args
expect_failure missing_input \
    "$TEST_ROOT/missing.nv12" "$TEST_ROOT/missing-output.nv12"

dd if=/dev/zero of="$TEST_ROOT/short.nv12" bs=1024 count=1 status=none
expect_failure short_input \
    "$TEST_ROOT/short.nv12" "$TEST_ROOT/short-output.nv12"
[[ ! -e $TEST_ROOT/short-output.nv12 ]] ||
    fail 'short-input failure left an output file'

OUTPUT_A="$TEST_ROOT/output-a.nv12"
OUTPUT_B="$TEST_ROOT/output-b.nv12"

"$BIN" "$INPUT" "$OUTPUT_A" | tee "$TEST_ROOT/run-a.log"
"$BIN" "$INPUT" "$OUTPUT_B" | tee "$TEST_ROOT/run-b.log"

[[ $(stat -c '%s' "$OUTPUT_A") -eq 1382400 ]] ||
    fail 'output size is not 1382400 bytes'
cmp -s "$OUTPUT_A" "$OUTPUT_B" || fail 'repeated output differs'

od -An -tu1 -v "$OUTPUT_A" |
awk '
{
    for (i = 1; i <= NF; i++) {
        if (($i + 0) != 0)
            nonzero = 1
    }
}
END { exit nonzero ? 0 : 1 }
' || fail 'output is all zero'

grep -Fq 'warmups=5' "$TEST_ROOT/run-a.log" ||
    fail 'missing warmup count'
grep -Fq 'iterations=100' "$TEST_ROOT/run-a.log" ||
    fail 'missing iteration count'
grep -Fq 'RGA_RESIZE_OK' "$TEST_ROOT/run-a.log" ||
    fail 'missing success marker'

sha256sum "$OUTPUT_A"
printf 'PASS: RGA NV12 resize tests\n'
```

- [ ] **Step 2: 检查脚本语法**

```bash
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh
```

Expected：无输出，退出状态为 0。

- [ ] **Step 3: 运行测试并确认它因程序尚不存在而失败**

```bash
mkdir -p /tmp/rga-empty-bundle
bash ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh \
    /tmp/rga-empty-bundle /tmp/not-created.nv12
```

Expected：非零退出，并报告 `FAIL: missing executable`。这个失败证明测试确实在
约束待实现产物。

- [ ] **Step 4: 提交失败测试**

```bash
git add ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh
git commit -m "test(rga): define nv12 resize acceptance"
```

### Task 3: 实现最小 RGA 缩放程序和构建入口

**Files:**
- Create: `ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp`
- Create: `ov13850_opi5pro_learning/rga/Makefile`
- Create: `ov13850_opi5pro_learning/rga/.gitignore`

- [ ] **Step 1: 实现固定格式文件缩放程序**

创建 `ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp`：

```cpp
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "im2d.hpp"

namespace {

constexpr int kSrcWidth = 1920;
constexpr int kSrcHeight = 1080;
constexpr int kDstWidth = 1280;
constexpr int kDstHeight = 720;
constexpr int kWarmups = 5;
constexpr int kIterations = 100;
constexpr int kFormat = RK_FORMAT_YCbCr_420_SP;
constexpr std::size_t kSrcSize =
    static_cast<std::size_t>(kSrcWidth) * kSrcHeight * 3 / 2;
constexpr std::size_t kDstSize =
    static_cast<std::size_t>(kDstWidth) * kDstHeight * 3 / 2;

class ImportedBuffer {
public:
    ImportedBuffer(void *address, std::size_t size)
        : handle_(importbuffer_virtualaddr(address, static_cast<int>(size)))
    {
    }

    ~ImportedBuffer()
    {
        if (handle_ != 0)
            releasebuffer_handle(handle_);
    }

    ImportedBuffer(const ImportedBuffer &) = delete;
    ImportedBuffer &operator=(const ImportedBuffer &) = delete;

    bool valid() const
    {
        return handle_ != 0;
    }

    rga_buffer_handle_t get() const
    {
        return handle_;
    }

private:
    rga_buffer_handle_t handle_ = 0;
};

bool read_exact_frame(const char *path, std::vector<unsigned char> &buffer)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);

    if (!input) {
        std::cerr << "ERROR: cannot open input: " << path << '\n';
        return false;
    }

    const std::streamoff size = input.tellg();
    if (size != static_cast<std::streamoff>(buffer.size())) {
        std::cerr << "ERROR: input size=" << size
                  << " expected=" << buffer.size() << '\n';
        return false;
    }

    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(buffer.data()),
               static_cast<std::streamsize>(buffer.size()));
    if (!input) {
        std::cerr << "ERROR: failed to read complete input frame\n";
        return false;
    }

    return true;
}

bool write_exact_frame(const char *path,
                       const std::vector<unsigned char> &buffer)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output) {
        std::cerr << "ERROR: cannot create output: " << path << '\n';
        return false;
    }

    output.write(reinterpret_cast<const char *>(buffer.data()),
                 static_cast<std::streamsize>(buffer.size()));
    output.close();
    if (!output) {
        std::cerr << "ERROR: failed to write complete output frame\n";
        std::remove(path);
        return false;
    }

    return true;
}

bool resize_once(const rga_buffer_t &src, rga_buffer_t &dst)
{
    const IM_STATUS status = imresize(src, dst);

    if (status != IM_STATUS_SUCCESS) {
        std::cerr << "ERROR: imresize failed: " << imStrError(status) << '\n';
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "ERROR: usage: " << argv[0]
                  << " <input-1920x1080.nv12> <output-1280x720.nv12>\n";
        return 2;
    }

    if (std::strcmp(argv[1], argv[2]) == 0) {
        std::cerr << "ERROR: input and output paths must differ\n";
        return 2;
    }

    std::remove(argv[2]);

    try {
        std::vector<unsigned char> src_data(kSrcSize);
        std::vector<unsigned char> dst_data(kDstSize, 0);

        if (!read_exact_frame(argv[1], src_data))
            return 3;

        ImportedBuffer src_handle(src_data.data(), src_data.size());
        ImportedBuffer dst_handle(dst_data.data(), dst_data.size());

        if (!src_handle.valid() || !dst_handle.valid()) {
            std::cerr << "ERROR: importbuffer_virtualaddr failed\n";
            return 4;
        }

        rga_buffer_t src = wrapbuffer_handle(
            src_handle.get(), kSrcWidth, kSrcHeight, kFormat);
        rga_buffer_t dst = wrapbuffer_handle(
            dst_handle.get(), kDstWidth, kDstHeight, kFormat);
        im_rect empty = {};
        const IM_STATUS check = imcheck(src, dst, empty, empty);

        if (check != IM_STATUS_NOERROR) {
            std::cerr << "ERROR: imcheck failed: " << imStrError(check) << '\n';
            return 5;
        }

        const char *version = querystring(RGA_VERSION);
        std::cout << "librga=" << (version != nullptr ? version : "unknown")
                  << '\n';
        std::cout << "input=1920x1080 NV12 bytes=" << kSrcSize << '\n';
        std::cout << "output=1280x720 NV12 bytes=" << kDstSize << '\n';
        std::cout << "warmups=" << kWarmups
                  << " iterations=" << kIterations << '\n';

        for (int i = 0; i < kWarmups; ++i) {
            if (!resize_once(src, dst))
                return 6;
        }

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            if (!resize_once(src, dst))
                return 6;
        }
        const auto end = std::chrono::steady_clock::now();

        if (!write_exact_frame(argv[2], dst_data))
            return 7;

        const double total_us =
            std::chrono::duration<double, std::micro>(end - start).count();
        const double average_us = total_us / kIterations;
        const double operations_per_second =
            average_us > 0.0 ? 1000000.0 / average_us : 0.0;

        std::cout << std::fixed << std::setprecision(2)
                  << "total_us=" << total_us
                  << " average_us=" << average_us
                  << " operations_per_second=" << operations_per_second
                  << '\n';
        std::cout << "RGA_RESIZE_OK\n";
    } catch (const std::exception &error) {
        std::cerr << "ERROR: exception: " << error.what() << '\n';
        return 8;
    }

    return 0;
}
```

- [ ] **Step 2: 创建交叉构建和 bundle 规则**

创建 `ov13850_opi5pro_learning/rga/Makefile`：

```make
CXX ?= aarch64-linux-gnu-g++

ROOT := $(CURDIR)
VENDOR := $(ROOT)/third_party/librga
BUILD := $(ROOT)/build/rga_nv12_resize
BIN := $(BUILD)/bin/rga_nv12_resize
LIB := $(BUILD)/lib/librga.so
SOURCE := $(ROOT)/src/rga_nv12_resize.cpp

CPPFLAGS += -I$(VENDOR)/include
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
LDFLAGS += -L$(VENDOR)/lib/aarch64
LDFLAGS += -Wl,--enable-new-dtags,-rpath,'$$ORIGIN/../lib'
LDLIBS += -lrga -pthread

.PHONY: all bundle clean

all: bundle

bundle: $(BIN) $(LIB)

$(BIN): $(SOURCE)
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

$(LIB): $(VENDOR)/lib/aarch64/librga.so
	mkdir -p $(dir $@)
	cp $< $@

clean:
	rm -rf $(ROOT)/build
```

- [ ] **Step 3: 排除本地构建产物**

创建 `ov13850_opi5pro_learning/rga/.gitignore`：

```gitignore
/build/
```

- [ ] **Step 4: 构建部署包**

```bash
cd ov13850_opi5pro_learning/rga
command -v aarch64-linux-gnu-g++
make clean
make bundle
```

Expected：编译无 warning，生成：

```text
build/rga_nv12_resize/bin/rga_nv12_resize
build/rga_nv12_resize/lib/librga.so
```

- [ ] **Step 5: 验证架构、依赖和相对 RUNPATH**

```bash
file build/rga_nv12_resize/bin/rga_nv12_resize
file build/rga_nv12_resize/lib/librga.so
readelf -d build/rga_nv12_resize/bin/rga_nv12_resize |
    grep -E 'NEEDED|RUNPATH'
sha256sum build/rga_nv12_resize/lib/librga.so
```

Expected：两个 ELF 均为 ARM aarch64；程序依赖 `librga.so`，RUNPATH 为
`$ORIGIN/../lib`；库校验值为
`e150bda757fb5e8a649c429ec7cabaf851aa2a3be554ed494d5519e8790d943b`。

- [ ] **Step 6: 提交实现**

```bash
git add \
    ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp \
    ov13850_opi5pro_learning/rga/Makefile \
    ov13850_opi5pro_learning/rga/.gitignore
git commit -m "feat(rga): add fixed nv12 resize experiment"
```

### Task 4: 写使用说明并完成本地静态验证

**Files:**
- Create: `ov13850_opi5pro_learning/rga/README.md`

- [ ] **Step 1: 编写学习和运行说明**

创建 `ov13850_opi5pro_learning/rga/README.md`：

```markdown
# Orange Pi 5 Pro RGA NV12 Resize

这个目录用于独立学习 RK3588S RGA 用户态接口。第一版读取一帧
1920x1080 NV12，通过官方 IM2D `imresize` 缩放为 1280x720 NV12。

它验证的是 RGA 最小闭环，不是最终实时摄像头程序。实时版本还需要处理
V4L2 队列、DMA-BUF fd、缓存同步和缓冲区生命周期。

## Dependency

项目固定使用官方 librga API 1.10.6_[3]，提交
`2b32edcb97b601b25683e2941d888c8515da6d55`。来源和校验值见
`third_party/librga/ORIGIN.md`。

## Build

```bash
cd ov13850_opi5pro_learning/rga
make clean bundle
```

生成的可部署目录是 `build/rga_nv12_resize/`。程序通过
`$ORIGIN/../lib` 加载该目录中的 `librga.so`，不会安装系统库。

## Input and output

- 输入：1920x1080 NV12，3,110,400 字节；
- 输出：1280x720 NV12，1,382,400 字节；
- 预热：5 次；
- 计时：100 次同步 `imresize`；
- 计时不包含文件 I/O 和缓冲区导入。

## Run on Orange Pi 5 Pro

```bash
./bin/rga_nv12_resize input-1920x1080.nv12 output-1280x720.nv12
```

成功时最后一行是 `RGA_RESIZE_OK`。

## Test on Orange Pi 5 Pro

```bash
bash test_rga_nv12_resize.sh \
    ./rga_nv12_resize-bundle \
    ./input-1920x1080.nv12
```
```

- [ ] **Step 2: 运行本地验证**

```bash
cd ~/linux-orangepi/.worktrees/rga-nv12-file-resize
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh
make -C ov13850_opi5pro_learning/rga clean bundle
git diff --check
git status -sb
```

Expected：脚本语法通过，bundle 重新构建成功，`git diff --check` 无输出。

- [ ] **Step 3: 提交说明文档**

```bash
git add ov13850_opi5pro_learning/rga/README.md
git commit -m "docs(rga): explain nv12 resize experiment"
```

### Task 5: 部署到板端并完成实机验证

**Files:**
- Create: `docs/codex/rga_nv12_file_resize_validation.md`
- Modify: `docs/codex/HANDOFF.md`

- [ ] **Step 1: 打包可执行文件、动态库和测试脚本**

```bash
cd ~/linux-orangepi/.worktrees/rga-nv12-file-resize
RGA_ROOT=ov13850_opi5pro_learning/rga
RELEASE=/tmp/rga-nv12-file-resize-stage3

mkdir -p "$RELEASE/rga-nv12-file-resize-bundle"
cp -a "$RGA_ROOT/build/rga_nv12_resize/bin" \
    "$RELEASE/rga-nv12-file-resize-bundle/"
cp -a "$RGA_ROOT/build/rga_nv12_resize/lib" \
    "$RELEASE/rga-nv12-file-resize-bundle/"
cp "$RGA_ROOT/tests/test_rga_nv12_resize.sh" "$RELEASE/"
cp ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh "$RELEASE/"

tar -C /tmp -czf /tmp/rga-nv12-file-resize-stage3.tar.gz \
    rga-nv12-file-resize-stage3
sha256sum /tmp/rga-nv12-file-resize-stage3.tar.gz
```

- [ ] **Step 2: 传输到当前 Orange Pi 5 Pro**

```bash
scp /tmp/rga-nv12-file-resize-stage3.tar.gz \
    orangepi@192.168.1.10:/tmp/
```

板端执行：

```bash
cd /tmp
tar -xzf rga-nv12-file-resize-stage3.tar.gz
cd /tmp/rga-nv12-file-resize-stage3
```

- [ ] **Step 3: 记录板端兼容性基线**

```bash
uname -r
test -c /dev/rga
sudo cat /sys/kernel/debug/rkrga/driver_version 2>/dev/null ||
    cat /proc/rkrga/driver_version
file rga-nv12-file-resize-bundle/bin/rga_nv12_resize
ldd rga-nv12-file-resize-bundle/bin/rga_nv12_resize
```

Expected：驱动为 v1.3.7，程序为 aarch64，`librga.so` 解析到 bundle 的
`lib/librga.so`。

- [ ] **Step 4: 配置 RKISP 并重新采集输入帧**

```bash
chmod +x configure_rkisp_1080p.sh
./configure_rkisp_1080p.sh

INPUT=/tmp/rga-input-1920x1080.nv12
v4l2-ctl -d /dev/video11 \
    --stream-mmap=4 \
    --stream-skip=3 \
    --stream-count=1 \
    --stream-to="$INPUT" \
    --stream-poll

stat -c '%s %n' "$INPUT"
sha256sum "$INPUT"
```

Expected：输入文件大小严格为 3,110,400 字节。

- [ ] **Step 5: 运行黑盒验收**

```bash
chmod +x test_rga_nv12_resize.sh \
    rga-nv12-file-resize-bundle/bin/rga_nv12_resize

bash ./test_rga_nv12_resize.sh \
    ./rga-nv12-file-resize-bundle \
    "$INPUT" | tee /tmp/rga-nv12-file-resize-validation.log
```

Expected：看到 librga 信息、`warmups=5 iterations=100`、耗时统计、
`RGA_RESIZE_OK`，最后为 `PASS: RGA NV12 resize tests`。

- [ ] **Step 6: 检查输出和内核日志**

```bash
OUTPUT=/tmp/rga-output-1280x720.nv12
rga-nv12-file-resize-bundle/bin/rga_nv12_resize \
    "$INPUT" "$OUTPUT"

stat -c '%s %n' "$OUTPUT"
sha256sum "$OUTPUT"

Y_SIZE=$((1280 * 720))
UV_SIZE=$((Y_SIZE / 2))

head -c "$Y_SIZE" "$OUTPUT" | od -An -tu1 -v |
    awk '{ for (i=1; i<=NF; i++) { v=$i+0; if (!n || v<min) min=v; if (!n || v>max) max=v; n++ } } END { printf "Y bytes=%d min=%d max=%d\n", n, min, max }'
tail -c "$UV_SIZE" "$OUTPUT" | od -An -tu1 -v |
    awk '{ for (i=1; i<=NF; i++) { v=$i+0; if (!n || v<min) min=v; if (!n || v>max) max=v; n++ } } END { printf "UV bytes=%d min=%d max=%d\n", n, min, max }'

sudo dmesg | grep -Ei 'rga|mmu fault|iommu fault' | tail -120
```

Expected：输出大小 1,382,400 字节；Y 和 UV 各自具有有效范围；日志没有新增
RGA、MMU 或 IOMMU fault。

- [ ] **Step 7: 写入实机证据和交接状态**

从 Steps 3-6 的终端原始输出中提取内核版本、输入与输出 SHA-256、平均耗时和
推算吞吐率。使用 `apply_patch` 创建
`docs/codex/rga_nv12_file_resize_validation.md`，文档必须包含以下确定字段：

```text
标题：RGA NV12 File Resize Validation
日期：2026-08-24
板卡：Orange Pi 5 Pro, RK3588S
内核：uname -r 的完整实测输出
RGA 驱动：v1.3.7
librga API：1.10.6_[3]
输入：1920x1080 NV12、3110400 字节、实测 SHA-256
输出：1280x720 NV12、1382400 字节、实测 SHA-256
预热次数：5
计时次数：100
平均耗时：程序报告的 average_us
推算吞吐率：程序报告的 operations_per_second
重复输出：identical
内核 RGA/MMU fault：none observed
结果：PASS
```

把 Steps 3、5、6 的关键原始输出放入文档中的 fenced code blocks，确保结论可
追溯。不得保留尖括号占位符或根据印象填写数值。

在 `docs/codex/HANDOFF.md` 的阶段 3 部分追加简短结论：文件到文件 RGA
缩放已验证，下一项是单独设计 V4L2 DMA-BUF 到 RGA 的实时链路。

- [ ] **Step 8: 提交验证证据**

```bash
git add \
    docs/codex/rga_nv12_file_resize_validation.md \
    docs/codex/HANDOFF.md
git commit -m "docs: record rga nv12 resize validation"
```

### Task 6: 最终复核并合并到 main

**Files:**
- Verify all files created in Tasks 1-5.

- [ ] **Step 1: 运行最终本地检查**

```bash
cd ~/linux-orangepi/.worktrees/rga-nv12-file-resize
bash -n ov13850_opi5pro_learning/rga/tests/test_rga_nv12_resize.sh
make -C ov13850_opi5pro_learning/rga clean bundle
git diff --check
git status -sb
git log --oneline main..HEAD
```

Expected：构建成功、diff 检查无输出、worktree 干净，并列出本计划产生的提交。

- [ ] **Step 2: 确认主仓库没有新增冲突**

```bash
cd ~/linux-orangepi
git status -sb
git log --oneline -5
```

Expected：仅保留用户已有的 `docs/codex/task_plan.md` 修改；若出现其他改动，
先停下审查，不覆盖或回退。

- [ ] **Step 3: 经用户确认后快进合并**

```bash
cd ~/linux-orangepi
git merge --ff-only codex/rga-nv12-file-resize
git status -sb
```

Expected：快进合并成功，`docs/codex/task_plan.md` 的既有修改仍保留。

- [ ] **Step 4: 用户确认推送后再推送**

```bash
git push origin main
```

不自动执行推送。完成后再清理 worktree 和临时分支。
