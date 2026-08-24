# Orange Pi 5 Pro RK MPP Hardware Encoding Design

日期：2026-08-25

## 1. 背景与已验证输入

阶段 3 已提供稳定编码输入：

```text
OV13850 RAW10 -> CIF/RKISP -> /dev/video11 -> 1920x1080 NV12@30
```

已验证 copy/direct 两种实时 RGA 路径均可持续处理 300 帧、30.04 fps、0
timeout/drop。阶段 4 不再改变 sensor、DTS、RKISP 或 RGA 基线，目标是把 NV12
交给 RK MPP，生成可完整解码的 H.264/H.265 硬件码流。

## 2. 板端环境事实

内核侧：

- Kernel: `6.1.99-opi5pro-livecfg-baseline`；
- `/dev/mpp_service` 存在，权限允许 video group 使用；
- `/proc/mpp_service/supports-device` 包含：
  - `RKVENC HW_ID: 0x50603312`；
  - `VEPU2`；
- RKVENC core0/core1 均 attach CCU 并 probe finish；
- Kconfig 内建 MPP service、RKVENC/RKVENC2、VEPU1/VEPU2；
- Orange Pi 5 Pro DTS 启用 RKVENC CCU、两个 RKVENC core 及 IOMMU。

用户态缺失：

- 无 `librockchip_mpp.so`；
- 无 MPP public headers；
- 无 `mpi_enc_test` / `mpp_info_test`；
- 无 MPP GStreamer plugin；
- 无 FFmpeg MPP encoder。

启动日志中两个 RKVENC core 报 VENC regulator/devfreq 初始化失败，但随后均 attach
CCU 并 probe finish。该告警先作为观测项：必须通过实际编码判断是否阻塞，不在
编码测试前修改 DTS、regulator 或 OPP。

## 3. 依赖选择

使用 Rockchip 官方 MPP：

- Repository: `https://github.com/rockchip-linux/mpp`；
- Tag: `1.1.0`；
- Commit: `c08762ebfadeb4e986d2fed993bc7a54862d3ebe`；
- Target: RK3588 / aarch64 Linux；
- Build: official CMake out-of-source aarch64 toolchain；
- Public API: official `inc/` MPI headers；
- Reference: official `mpi_enc_test`。

仓库不 vendor 完整 MPP 源码，也不提交构建二进制。项目提交固定来源清单和
fetch/build 脚本；脚本下载固定提交到被忽略的 `build/source`，安装 SDK 到
`build/sdk`，最终生成自包含 bundle。

板端不执行系统级 MPP 安装。程序通过相对 RUNPATH 加载 bundle 中的
`librockchip_mpp.so`，避免 `/usr` 或 `/usr/local` 中的版本污染。

## 4. 阶段拆分

阶段 4 严格按以下顺序：

```text
4A 官方 MPP 构建与 bundle
4B 官方样例编码验证
4C 自定义 H.264 文件编码器
4D 参数能力与 H.265
4E V4L2 实时 H.264 copy path
4F DMA-BUF 可行性
4G 阶段验收与文档
```

编码、网络推流和 DMA-BUF 不得在同一个首次验证中混合。

## 5. 目录结构

```text
ov13850_opi5pro_learning/mpp/
├── .gitignore
├── ORIGIN.md
├── README.md
├── scripts/
│   ├── fetch_build_mpp.sh
│   ├── package_mpp_bundle.sh
│   └── validate_bitstream.sh
├── src/
│   ├── nv12_mpp_encoder.cpp
│   └── v4l2_mpp_encoder.cpp
├── tests/
│   ├── test_mpp_file_encoder.sh
│   ├── test_mpp_live_encoder.sh
│   └── test_mpp_parameters.sh
└── build/                         # ignored
    ├── source/mpp/
    ├── cmake-aarch64/
    ├── sdk/
    └── bundle/
```

## 6. 4A 官方 MPP 构建

`fetch_build_mpp.sh`：

1. 检查 cmake、make 和 `aarch64-linux-gnu-gcc/g++`；
2. clone 官方仓库到 `build/source/mpp`；
3. checkout detached 固定 commit；
4. 验证 HEAD 精确匹配；
5. 使用官方 `build/linux/aarch64/arm.linux.cross.cmake`；
6. Release out-of-source build；
7. 安装到 `build/sdk`；
8. 验证 public headers、shared library、`mpi_enc_test` 和 `mpp_info_test`；
9. 记录 SHA-256 和 ELF aarch64 架构。

构建脚本重复执行必须幂等；固定提交不匹配时停止，不能静默更新到新版本。

Bundle 至少包含：

```text
bin/mpi_enc_test
bin/mpp_info_test
bin/nv12_mpp_encoder
bin/v4l2_mpp_encoder
lib/librockchip_mpp.so*
share/ORIGIN.md
```

## 7. 4B 官方样例验证

先用官方工具验证环境：

1. `mpp_info_test` 输出 library/build/compatibility 信息；
2. `mpi_enc_test` 使用官方生成帧编码 H.264；
3. 使用实际 RKISP 1920x1080 NV12 单帧做一帧 H.264 编码；
4. 记录编码前后 `/proc/mpp_service/sessions-summary`；
5. 只检查本次新增的 MPP/RKVENC/IOMMU 日志。

若官方样例失败，停止编写自定义 encoder，先按以下层次定位：

```text
用户态 MPP 版本/架构/RUNPATH
-> /dev/mpp_service 权限和 ioctl 兼容
-> RKVENC/IOMMU task 日志
-> devfreq/regulator 告警是否真正阻塞
```

## 8. 4C H.264 文件编码器

第一条固定基线：

```text
Input     : 1920x1080 NV12
Frames    : 300
FPS       : 30/1
Codec     : H.264 AVC Annex-B
Rate ctrl : CBR
Bitrate   : 8,000,000 bps
GOP       : 60
B frames  : 0
Output    : elementary .h264
```

程序流程：

```text
mpp_create
-> mpp_init(MPP_CTX_ENC, MPP_VIDEO_CodingAVC)
-> mpp_enc_cfg_init/get/set
-> prep: width/height/stride/format
-> rc: fps/bps/gop/drop policy
-> codec: H.264 profile/level/entropy
-> MPP_ENC_SET_CFG
-> MPP_ENC_GET_HDR_SYNC (SPS/PPS)
-> frame loop
-> EOS
-> mpp_reset / mpp_destroy
```

### 8.1 Stride 与输入复制

输入文件是紧凑 NV12：

```text
Y  : 1920 * 1080
UV : 1920 * 540
```

MPP buffer 使用：

```text
hor_stride = align(1920, 16) = 1920
ver_stride = align(1080, 16) = 1088
buffer_size = hor_stride * ver_stride * 3 / 2
```

每帧逐行复制 Y 的 1080 行和 UV 的 540 行，并清零 vertical padding。禁止直接
把 3,110,400-byte 紧凑文件当成 1088 高 buffer，否则 UV offset 错误。

单帧文件在 300 帧测试中循环使用，便于隔离编码器；每帧设置递增 PTS 和 frame
index。最后一帧设置 EOS。

### 8.2 Packet 与码流

- 初始化时先写 SPS/PPS header；
- 每个 input frame 必须取出对应 output packet；
- packet length 为 0 时不写文件；
- 统计 packet 数、字节数、IDR 数和编码耗时；
- 输出 Annex-B elementary stream；
- 失败或短写时删除无效码流。

## 9. 4D 参数能力与 H.265

H.264 基线通过后验证：

- CBR 4/8/12 Mbps；
- GOP 30/60；
- VBR 8 Mbps；
- 运行中请求 IDR；
- H.265 8 Mbps、GOP 60、300 帧。

H.265 复用相同 frame/stride/buffer 逻辑，只切换 coding type 和 codec 配置。
低延迟基线不启用 B 帧。

## 10. 码流验证

WSL 安装标准 `ffmpeg`/`ffprobe`，仅作验证工具，不进入板端运行依赖。

每条码流检查：

- codec_name；
- 1920x1080；
- 30 fps 信令；
- 可完整解码 300 帧到 null sink；
- 无 decode error；
- H.264 SPS/PPS 或 H.265 VPS/SPS/PPS；
- 首帧 IDR；
- 实际平均码率与配置目标的偏差；
- 输出 SHA-256。

仅生成非空文件不算成功。

## 11. 4E V4L2 实时 H.264

新增 `v4l2_mpp_encoder`，第一版使用显式 copy：

```text
/dev/video11 DQBUF
-> memcpy/stride copy 到 MppBuffer
-> QBUF
-> MPP encode_put_frame/get_packet
-> .h264
```

固定预丢弃 3 帧、编码 300 帧。记录：

- capture sequence drop/timeout；
- stride copy 平均耗时；
- MPP 平均耗时；
- 完整 loop FPS；
- packet/IDR/byte 数；
- process user/system CPU；
- PTS 单调性。

完成标准：300 帧、约 30 fps、0 timeout/drop，码流可解码 300 帧，sensor PM
回到 suspended/usage 0，无新增 CIF/ISP/MPP/RKVENC/IOMMU fault。

## 12. 4F DMA-BUF 可行性

在 copy path 通过后检查 RKISP `VIDIOC_EXPBUF` 与 MPP external buffer import：

```text
V4L2 MMAP index -> EXPBUF fd -> MppBuffer import -> MPP
```

若支持并且缓存/所有权规则清楚，实现 300 帧原型并与 copy 比较。若接口、stride
或缓存同步无法可靠闭环，记录具体 ioctl/API/日志证据，将完整 DMA-BUF 优化移到
阶段 6；不得用未经验证的“零拷贝”阻塞阶段 4文件和实时编码主目标。

## 13. 错误处理与资源所有权

所有资源使用单一所有者和反向清理：

```text
packet/frame metadata
-> MppBuffer
-> MppBufferGroup
-> MppCtx/MppApi
-> V4L2 STREAMOFF/MMAP/fd
```

任何失败必须打印阶段和 MPP_RET；不能只输出“encode failed”。失败时不保留旧
输出或残缺码流。timeout、EOS、packet ownership 和 MPP reset/destroy 必须有
单独测试。

## 14. 阶段 4 完成标准

阶段 4完成必须同时满足：

1. 官方 MPP 1.1.0 构建和 bundle 可复现；
2. 官方编码样例在当前内核上工作；
3. 自定义 H.264/H.265 文件编码各 300 帧可完整解码；
4. 码率、FPS、GOP 和 IDR 可配置并有验证；
5. V4L2 -> MPP H.264 300 帧实时闭环通过；
6. 明确每条路径的 buffer ownership 和 copy 次数；
7. DMA-BUF 有通过原型或明确阻塞证据；
8. PM、MPP/RKVENC/IOMMU 日志无未解释错误；
9. 更新 task plan、HANDOFF、progress 和 troubleshooting。

阶段 4不包含 RTP/RTSP、播放器缓存或网络恢复；这些属于阶段 5。
