# RK3588 MPP Hardware Encoding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline. Do not dispatch subagents.

**Goal:** 在 Orange Pi 5 Pro 上建立可复现的官方 MPP 1.1.0 环境，完成 1080p30 H.264/H.265 文件与实时硬件编码，并验证参数、码流、所有权和 DMA-BUF 可行性。

**Architecture:** 先用官方 `mpi_enc_test` 验证用户库与内核 RKVENC，再实现项目内文件编码器，最后接入 `/dev/video11`。首版实时路径显式复制到 MppBuffer；DMA-BUF 仅在 copy path 稳定后验证。

**Tech Stack:** Rockchip MPP 1.1.0, CMake, aarch64-linux-gnu, C++17, V4L2, ffmpeg/ffprobe, Bash.

---

### Task 1: 隔离 worktree 与官方依赖脚手架

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/.gitignore`
- Create: `ov13850_opi5pro_learning/mpp/ORIGIN.md`
- Create: `ov13850_opi5pro_learning/mpp/scripts/fetch_build_mpp.sh`
- Create: `ov13850_opi5pro_learning/mpp/tests/test_fetch_build_mpp.sh`

- [ ] 创建 `codex/mpp-stage4` worktree，确认 `.worktrees` 被忽略、基线干净。
- [ ] 测试脚本先要求固定 commit、aarch64 library、public headers、
`mpi_enc_test`、`mpp_info_test` 和 SHA 清单；在构建脚本不存在时观察 RED。
- [ ] 构建脚本固定 tag `1.1.0` / commit
`c08762ebfadeb4e986d2fed993bc7a54862d3ebe`，clone 到 `build/source/mpp`。
- [ ] 使用官方 aarch64 CMake toolchain，Release out-of-source build，并将产物整理到
`build/sdk/{bin,include,lib}`。
- [ ] 运行测试转 GREEN，提交：

```bash
git commit -m "build(mpp): add pinned official sdk builder"
```

### Task 2: 官方 MPP bundle 与板端样例

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/scripts/package_mpp_bundle.sh`
- Create: `ov13850_opi5pro_learning/mpp/tests/test_official_mpp_board.sh`
- Create: `docs/codex/mpp_official_environment_validation.md`

- [ ] Bundle 携带 `librockchip_mpp.so*`、`mpi_enc_test`、`mpp_info_test`、来源和 SHA。
- [ ] 两个 binary RUNPATH 通过 `LD_LIBRARY_PATH` wrapper 或相对 lib 路径运行，不做系统安装。
- [ ] 板端校验 `/dev/mpp_service`、RKVENC support、library architecture 和 SHA。
- [ ] 运行 `mpp_info_test`。
- [ ] 读取 `mpi_enc_test --help`，用实际参数编码官方生成帧 H.264。
- [ ] 再对实际 1920x1080 NV12 单帧编码至少一帧。
- [ ] 检查输出非空、MPP session 和新增 RKVENC/IOMMU 日志。
- [ ] 记录 devfreq regulator 告警是否阻塞，提交：

```bash
git commit -m "docs: record official mpp environment validation"
```

### Task 3: H.264 文件编码器

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`
- Create: `ov13850_opi5pro_learning/mpp/Makefile`
- Create: `ov13850_opi5pro_learning/mpp/tests/test_mpp_file_encoder.sh`

- [ ] 黑盒测试定义固定 1920x1080、300 帧、H.264、8 Mbps、GOP60、输出字段和失败行为，先观察缺 binary RED。
- [ ] 实现 MPP context/config/header/frame/packet/EOS RAII。
- [ ] `hor_stride=1920`、`ver_stride=1088`；逐行复制 Y 1080 行和 UV 540 行，padding 清零。
- [ ] 单帧输入循环编码 300 次，PTS 递增；输出 H.264 Annex-B。
- [ ] 输出 frame/packet/IDR/bytes、FPS、CPU 和耗时。
- [ ] `make clean bundle` 生成相对 RUNPATH aarch64 binary。
- [ ] 板端 300 帧编码，提交：

```bash
git commit -m "feat(mpp): add h264 nv12 file encoder"
```

### Task 4: 码流验证、参数矩阵与 H.265

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/scripts/validate_bitstream.sh`
- Create: `ov13850_opi5pro_learning/mpp/tests/test_mpp_parameters.sh`
- Modify: `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`

- [ ] WSL 安装 ffmpeg/ffprobe 作为验证工具。
- [ ] 验证 H.264 codec、1920x1080、30 fps、300 解码帧、SPS/PPS、首 IDR 和无 decode error。
- [ ] CLI 支持 `--codec h264|h265 --bitrate --gop --rc cbr|vbr --frames --request-idr`。
- [ ] 验证 H.264 4/8/12 Mbps、GOP30/60、CBR/VBR、运行中 IDR。
- [ ] 增加 H.265，验证 VPS/SPS/PPS、300 帧完整解码。
- [ ] 提交：

```bash
git commit -m "feat(mpp): add encoder parameter and h265 support"
```

### Task 5: V4L2 实时 H.264 copy path

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`
- Create: `ov13850_opi5pro_learning/mpp/tests/test_mpp_live_encoder.sh`
- Modify: `ov13850_opi5pro_learning/mpp/Makefile`

- [ ] 复用阶段 3 的 multiplanar MMAP capture 约束：4 buffers、预丢弃3、处理300、2秒 timeout。
- [ ] 每帧 `DQBUF -> stride copy 到 MppBuffer -> QBUF -> MPP`。
- [ ] PTS/sequence 单调，H.264 8 Mbps CBR、GOP60、B=0。
- [ ] 统计 copy、MPP、loop FPS、CPU、packet、IDR、timeout/drop。
- [ ] 板端 300 帧、约30fps、0 timeout/drop；WSL 解码300帧。
- [ ] PM suspended/0，无新增 CIF/ISP/MPP/RKVENC/IOMMU fault。
- [ ] 提交：

```bash
git commit -m "feat(mpp): add realtime v4l2 h264 encoder"
```

### Task 6: DMA-BUF 可行性

**Files:**
- Create: `docs/codex/mpp_dmabuf_feasibility.md`
- Modify only if supported: `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`

- [ ] 对 `/dev/video11` 的 4 个 index 测试 `VIDIOC_EXPBUF`。
- [ ] 检查 MPP external fd import、stride、缓存同步与所有权要求。
- [ ] 若支持，增加 `--dmabuf` 300 帧模式并与 copy 比较。
- [ ] 若不支持，记录 ioctl/API/日志证据，不用猜测实现阻塞主目标。
- [ ] 提交：

```bash
git commit -m "docs: record mpp dmabuf feasibility"
```

### Task 7: 阶段 4 文档与收口

**Files:**
- Create: `docs/codex/mpp_file_encoding_validation.md`
- Create: `docs/codex/mpp_live_encoding_validation.md`
- Modify: `docs/codex/task_plan.md`
- Modify: `docs/codex/HANDOFF.md`
- Modify: `docs/codex/progress.md`
- Modify: `docs/codex/orangepi5pro-kernel-troubleshooting.md`
- Modify: `ov13850_opi5pro_learning/orangepi5pro-kernel-troubleshooting.md`

- [ ] 汇总 H.264/H.265 文件与实时证据、参数矩阵、码流解码、CPU、PM 和日志。
- [ ] 解释 VENC regulator/devfreq 告警的最终影响。
- [ ] 阶段 4完成后将阶段 5标为当前；否则明确保留阻塞项。
- [ ] clean build、脚本语法、文档链接、双 troubleshooting hash、Git clean。
- [ ] fast-forward 合并 main，不自动 push。
