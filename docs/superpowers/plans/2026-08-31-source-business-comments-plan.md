# C/C++ Source Business Comments Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Do not use subagents for this project because the user explicitly requested inline execution.

**Goal:** Add beginner-friendly Chinese business comments to the 12 project-owned non-test `.c` and `.cpp` files without changing executable behavior.

**Architecture:** Comments follow the runtime data flow from OV13850 sensor through RKISP, optional RGA, MPP, RTP/RTSP, and RKAIQ. Every file receives a business-position header; core entry points and ownership/state transitions receive focused comments that explain inputs, outputs, lifecycle, and failure impact.

**Tech Stack:** Linux kernel C, V4L2, C++17, Rockchip RGA, Rockchip MPP, GStreamer RTP/RTSP, RKAIQ compatibility helpers, Git/Kbuild/cross-compilation.

---

### Task 1: Establish the comment-only baseline

**Files:**
- Read: all 12 target files from the design document
- Do not modify: test files, headers, scripts, Makefiles, DTS, third-party sources

- [x] **Step 1: Record the exact target list**

Run:

```bash
rg --files drivers/media/i2c ov13850_opi5pro_learning \
  | rg '\.(c|cpp)$' \
  | rg -v '(^|/)tests?/|third_party/' \
  | rg 'ov13850|ov13850_opi5pro_learning' \
  | sort
```

Expected: exactly the 12 files listed in the design, with no test path.

- [x] **Step 2: Verify the clean isolated branch**

Run: `git status -sb`

Expected: branch `codex/source-business-comments`, clean after the design and plan commits.

- [x] **Step 3: Run available baseline tests**

Run:

```bash
bash ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh
make -C ov13850_opi5pro_learning/rkaiq test
```

Expected: RKISP script test and RKAIQ host tests pass.

### Task 2: Comment the two OV13850 kernel drivers

**Files:**
- Modify: `drivers/media/i2c/ov13850.c`
- Modify: `drivers/media/i2c/ov13850_i2c_min.c`

- [x] **Step 1: Add file-level business maps**

At each file header, explain the sensor's place in the pipeline, the difference between the formal driver and the isolated learning driver, and the path:

```text
device tree match -> probe -> power on -> identify sensor -> register V4L2 subdev
-> configure mode/controls -> stream on -> runtime PM stream off
```

- [x] **Step 2: Explain sensor state and register tables**

Add comments around the private camera structure, mode descriptions, register arrays, supplies, controls, and endpoint data. Explain HTS/VTS, exposure, gain, link frequency, pixel rate, RAW10, and why register arrays are written in order.

- [x] **Step 3: Explain lifecycle functions**

Comment probe/remove, power on/off, runtime suspend/resume, format negotiation, control callbacks, start/stop streaming, and module-info ioctl. Each comment states caller, precondition, state transition, and cleanup behavior.

- [x] **Step 4: Inspect the diff for comment-only changes**

Run:

```bash
git diff --check -- drivers/media/i2c/ov13850.c \
  drivers/media/i2c/ov13850_i2c_min.c
git diff --word-diff=plain -- drivers/media/i2c/ov13850.c \
  drivers/media/i2c/ov13850_i2c_min.c
```

Expected: only comments and surrounding blank lines are added; no C token, constant, or control flow changes.

- [x] **Step 5: Commit the driver comments**

```bash
git add drivers/media/i2c/ov13850.c drivers/media/i2c/ov13850_i2c_min.c
git commit -m "docs(ov13850): explain sensor driver business flow"
```

### Task 3: Comment RGA file and live paths

**Files:**
- Modify: `ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp`
- Modify: `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp`

- [x] **Step 1: Explain file-mode resize**

Describe NV12 Y/UV layout, expected byte counts, source/destination image wrappers, warmup runs, timed runs, output validation, and why the RGA call is synchronous in this experiment.

- [x] **Step 2: Explain live capture and buffer ownership**

Describe `REQBUFS -> QUERYBUF -> mmap -> QBUF -> STREAMON -> poll/DQBUF -> RGA -> QBUF`, and contrast copy versus direct-MMAP. Explicitly explain that a dequeued V4L2 buffer cannot be returned to the driver until RGA has finished reading it.

- [x] **Step 3: Verify and commit**

Run:

```bash
git diff --check -- ov13850_opi5pro_learning/rga/src
make -C ov13850_opi5pro_learning/rga bundle
```

Expected: both aarch64 binaries build. Then commit:

```bash
git add ov13850_opi5pro_learning/rga/src/*.cpp
git commit -m "docs(rga): explain nv12 processing business flow"
```

### Task 4: Comment the two MPP front ends

**Files:**
- Modify: `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`
- Modify: `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`

- [x] **Step 1: Explain the file encoder front end**

Describe CLI configuration, one-frame NV12 reads, repeated encoding, EOS flushing, packet output, and the boundary between this front end and `MppEncoder` in the header.

- [x] **Step 2: Explain the live encoder front end**

Describe RKISP `/dev/video11` input, skipped startup frames, copy versus DMA-BUF paths, frame/packet ownership, timestamp/statistics collection, and runtime-PM cleanup after capture stops.

- [x] **Step 3: Verify and commit**

Run:

```bash
git diff --check -- ov13850_opi5pro_learning/mpp/src
make -C ov13850_opi5pro_learning/mpp test
```

Expected: packet-sink and V4L2 contract tests pass. Build the bundle when the pinned SDK is available. Then commit:

```bash
git add ov13850_opi5pro_learning/mpp/src/*.cpp
git commit -m "docs(mpp): explain encoding front-end business flow"
```

### Task 5: Comment RTP and RTSP source files

**Files:**
- Modify: `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp`

- [x] **Step 1: Explain RTP packet delivery**

Describe MPP packet to GStreamer appsrc transfer, caps, RTP payloader, UDP sink, timestamps, backpressure, and bus errors. Explain why codec headers and IDR frames matter to a receiver joining mid-stream.

- [x] **Step 2: Explain shared RTSP service behavior**

Describe the shared media factory, single camera/encoder worker, appsrc lifecycle, client connection/disconnection, codec-header replay, IDR request, and why capture continues across client reconnects.

- [x] **Step 3: Explain both executable orchestration loops**

For RTP, explain finite frame capture and queue-overrun recovery. For RTSP, explain the GLib main loop, background capture worker, monotonic PTS, signals, error propagation, and ordered shutdown.

- [x] **Step 4: Verify and commit**

Run:

```bash
git diff --check -- ov13850_opi5pro_learning/streaming/src
make -C ov13850_opi5pro_learning/streaming \
  smoke test-rtp-sink test-congestion test-live-pts
```

Expected: host-independent tests and build smoke targets pass with the pinned SDK/runtime available. Then commit:

```bash
git add ov13850_opi5pro_learning/streaming/src/*.cpp
git commit -m "docs(streaming): explain rtp and rtsp business flow"
```

### Task 6: Comment RKAIQ module-info helpers

**Files:**
- Modify: `ov13850_opi5pro_learning/rkaiq/src/rkmodule_info_probe.c`
- Modify: `ov13850_opi5pro_learning/rkaiq/src/rkmodule_info_preload.c`

- [x] **Step 1: Explain the probe utility**

Describe why RKAIQ needs sensor/module/lens names, how the V4L2 subdev ioctl provides them, and how this tiny utility distinguishes kernel metadata failure from an IQ-file problem.

- [x] **Step 2: Explain the preload compatibility boundary**

Describe `LD_PRELOAD`, `dlsym(RTLD_NEXT)`, the explicit environment gate, target-ioctl filtering, fallback metadata, and why unrelated ioctls must pass through unchanged.

- [x] **Step 3: Verify and commit**

Run:

```bash
make -C ov13850_opi5pro_learning/rkaiq clean test
git diff --check -- ov13850_opi5pro_learning/rkaiq/src
```

Expected: host tools compile with `-Werror` and all RKAIQ tests pass. Then commit:

```bash
git add ov13850_opi5pro_learning/rkaiq/src/*.c
git commit -m "docs(rkaiq): explain module-info compatibility flow"
```

### Task 7: Final behavior-preservation verification

**Files:**
- Verify: all 12 target source files
- Verify unchanged: all tests and out-of-scope files

- [x] **Step 1: Confirm scope**

Run:

```bash
git diff --name-only e6bb1084f..HEAD
```

Expected: the design/plan documents and exactly 12 target `.c/.cpp` files; no test, header, script, Makefile, DTS, or third-party file.

- [x] **Step 2: Audit additions and deletions**

Run:

```bash
git diff --check e6bb1084f..HEAD
git diff --numstat e6bb1084f..HEAD -- '*.c' '*.cpp'
git diff --word-diff=plain e6bb1084f..HEAD -- '*.c' '*.cpp'
```

Expected: source changes are explanatory comments/blank lines only. Any executable token change blocks completion.

- [x] **Step 3: Run the complete available verification set**

Run the RKISP script test, RKAIQ tests, RGA bundle build, MPP tests, streaming tests, and targeted kernel object builds. Record unavailable dependency-driven checks explicitly rather than claiming they passed.

- [x] **Step 4: Update the source index**

Modify `docs/codex/project_source_file_index.md` only to add a short note that the 12 C/C++ learning files now contain zero-basis business comments. Do not expand the index into a duplicate tutorial.

- [x] **Step 5: Commit final index update**

```bash
git add docs/codex/project_source_file_index.md
git commit -m "docs(code): index beginner-commented sources"
```

- [ ] **Step 6: Merge back without touching the user's existing change**

Verify the main worktree still contains only the user's pre-existing troubleshooting edit, then fast-forward or cherry-pick the isolated branch commits. Do not stage or rewrite that file.

## Execution Notes

- `COMMENT_ONLY_CHECK=OK files=12`：基线与当前文件剥离C/C++注释和布局后完全一致。
- 两个OV13850驱动目标由Kbuild重新编译成功。
- 两个RGA源文件通过aarch64 `-Wall -Wextra -Werror -fsyntax-only`；完整bundle
  链接因未跟踪的`librga.so`在当前机器已不存在而不可执行。
- 两个MPP前端通过aarch64严格语法检查，并使用固定MPP SDK完整链接成功。
- streaming的congestion和live PTS测试通过；四个GStreamer源文件的完整编译因
  当前WSL没有GStreamer开发包且Docker服务未运行而不可执行。四个文件的token
  比较通过，源码可执行部分相对已验证基线没有变化。
- RKAIQ host测试、NV12统计测试、构建脚本契约测试和aarch64工具构建通过。
- 最终范围只有本设计/计划/索引和12个目标`.c/.cpp`，没有测试脚本、头文件、
  Makefile、DTS或第三方源码改动。
