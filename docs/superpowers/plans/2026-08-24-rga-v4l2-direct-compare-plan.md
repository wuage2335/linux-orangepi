# V4L2 Direct-MMAP RGA Comparison Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline. Do not dispatch subagents.

**Goal:** 为现有实时 RGA 程序增加向后兼容的 `--direct` 模式、统一进程 CPU 统计，并完成 bypass/copy/direct 三路径板端对比。

**Architecture:** Copy 与 direct 共用 VideoCapture、3+300 帧循环、sequence/timeout 和清理。Copy 继续 `DQ->memcpy->Q->RGA`；direct 在 STREAMON 前导入全部 MMAP 地址，并执行 `DQ->RGA->Q`。内置 getrusage 统计正式循环，外部 `/usr/bin/time -v` 用于三路径旁证。

**Tech Stack:** C++17, V4L2 MMAP, getrusage, Rockchip librga IM2D, Bash, GNU time, aarch64 cross toolchain.

---

### Task 1: 扩展黑盒测试并验证 RED

**Files:**
- Modify: `ov13850_opi5pro_learning/rga/tests/test_rga_v4l2_live.sh`
- Create: `ov13850_opi5pro_learning/rga/tests/benchmark_rga_v4l2_modes.sh`

- [ ] 在现有 copy 验收中增加：

```text
mode=copy
cpu_user_ms
cpu_system_ms
process_cpu_percent
```

- [ ] 新增 direct 调用：

```bash
"$BIN" --direct "$DEVICE" "$DIRECT_OUTPUT"
```

要求 mode=direct、300 frames、0 timeout/drop、copy_average_us=0.00、CPU/RGA/FPS
字段完整、输出 1,382,400 bytes 且非全零。

- [ ] benchmark 脚本依次以 `/usr/bin/time -v -o LOG` 运行：

```bash
v4l2-ctl -d "$DEVICE" --stream-mmap=4 --stream-count=300 --stream-to=/dev/null --stream-poll
"$BIN" "$DEVICE" "$COPY_OUTPUT"
"$BIN" --direct "$DEVICE" "$DIRECT_OUTPUT"
```

保留三份 time log 和两份程序 log。

- [ ] 运行 `bash -n`，然后用当前 binary 执行 direct 验证，预期因不支持
`--direct` 返回非零，形成 RED。

- [ ] 提交：

```bash
git commit -m "test(rga): define direct mmap comparison"
```

### Task 2: 实现 direct 模式和 CPU 统计

**Files:**
- Modify: `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp`

- [ ] 增加 `InputMode { Copy, Direct }`，保持两参数 copy CLI，增加四参数
`--direct` CLI。

- [ ] `VideoCapture` 增加只读 `mapped_views()`，不转移 mmap 所有权。

- [ ] 将现有 resizer 命名为 `CopyRgaResizer`；新增 `DirectRgaResizer`：
  - 构造时导入全部 MMAP 地址；
  - 每个 source 与共同 output 执行 imcheck；
  - `resize(index)` 同步处理对应 buffer；
  - RGA handles 在 VideoCapture munmap 前释放。

- [ ] 主循环保持 copy 顺序；direct 顺序改为 RGA 完成后 QBUF。

- [ ] 循环前后调用 `getrusage(RUSAGE_SELF)`，输出：

```text
cpu_user_ms
cpu_system_ms
process_cpu_percent
```

Direct 的 copy total/average 固定 0.00。

- [ ] 运行 `-Wall -Wextra -Werror` syntax/link、`git diff --check`，提交：

```bash
git commit -m "feat(rga): add direct mmap input mode"
```

### Task 3: 构建与说明

**Files:**
- Modify: `ov13850_opi5pro_learning/rga/README.md`

- [ ] Makefile 无需新增 binary；从零构建现有两个 binaries。
- [ ] README 记录 `--direct`、两种 QBUF 顺序、CPU 字段和 benchmark 命令。
- [ ] 两个测试脚本 `bash -n`，两个 binaries aarch64/RUNPATH 复核。
- [ ] 提交：

```bash
git commit -m "docs(rga): explain direct mmap comparison"
```

### Task 4: 板端三路径验收

**Files:**
- Create: `docs/codex/rga_v4l2_direct_comparison_validation.md`
- Modify: `docs/codex/HANDOFF.md`

- [ ] 打包 binary、librga、配置脚本、测试与 benchmark，生成 SHA 后 SCP。
- [ ] 配置并回读 `/dev/video11` 为 1920x1080 NV12。
- [ ] 运行 copy/direct 黑盒验收，要求各 300 帧、0 timeout/drop、约 30 fps。
- [ ] 运行 bypass/copy/direct benchmark，记录内部 CPU、外部 time、RSS、copy/RGA。
- [ ] 检查两个输出 Y/UV、PM suspended/0，以及新增内核日志 fault。
- [ ] 写三路径实测表；不预设 direct 优于 copy。
- [ ] 提交：

```bash
git commit -m "docs: record direct mmap rga comparison"
```

### Task 5: 阶段 3 收口

- [ ] clean build、脚本语法、Git clean、worktree/main 一致性验证。
- [ ] fast-forward 合并 main，不暂存或覆盖 main 中现有
`docs/codex/task_plan.md` 陈旧改动。
- [ ] HANDOFF 标记阶段 3 功能完成，说明 resize 是当前按需变换，旋转/色彩转换
无业务需求，DMA-BUF 留待低延迟阶段。
- [ ] 不自动 push；worktree 保留给阶段 4 或后续低延迟实验。
