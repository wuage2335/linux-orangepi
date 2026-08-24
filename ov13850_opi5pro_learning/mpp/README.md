# Orange Pi 5 Pro RK MPP Learning Project

本目录用于阶段 4 RK MPP 硬件编码学习，固定官方 MPP 1.1.0，并提供文件、实时
copy 和实时 DMA-BUF 三条路径。

## Dependency

```text
Repository: https://github.com/rockchip-linux/mpp
Tag: 1.1.0
Commit: c08762ebfadeb4e986d2fed993bc7a54862d3ebe
```

官方源码和生成 SDK 位于被忽略的 `build/`，不安装到板端系统目录。

## Build

```bash
./scripts/fetch_build_mpp.sh
make clean bundle
```

Bundle：

```text
build/bundle/official-mpp/
├── bin/mpi_enc_test
├── bin/mpi_dec_test
├── bin/mpp_info_test
├── bin/nv12_mpp_encoder
├── bin/v4l2_mpp_encoder
├── lib/librockchip_mpp.so*
└── SHA256SUMS
```

运行时使用 bundle 内 `librockchip_mpp.so`，不依赖系统安装。

## File Encoder

默认 H.264：

```bash
./bin/nv12_mpp_encoder input-1920x1080.nv12 output.h264
```

参数示例：

```bash
./bin/nv12_mpp_encoder \
    --codec h265 \
    --bitrate 8000000 \
    --gop 60 \
    --rc cbr \
    --frames 300 \
    --request-idr 150 \
    input.nv12 output.h265
```

输入是紧凑 1920x1080 NV12；copy path 将它逐行复制到 1920x1088 MppBuffer，
UV 从 `1920*1088` offset 开始。

## Live Encoder

先运行 RKISP 配置脚本，再执行：

```bash
# Copy path
./bin/v4l2_mpp_encoder /dev/video11 output-copy.h264

# DMA-BUF path
./bin/v4l2_mpp_encoder --dmabuf /dev/video11 output-dmabuf.h264
```

Copy：

```text
DQBUF -> stride copy to MppBuffer -> QBUF -> MPP
```

DMA-BUF：

```text
REQBUFS -> EXPBUF -> MppBuffer import
DQBUF -> MPP external frame -> QBUF
```

V4L2 的 NV12 UV 紧跟 1080 行 Y，因此 DMA-BUF context/frame 使用
`ver_stride=1080`；copy MppBuffer 使用 1088 padding。两者不能混用。

## Validation

```bash
bash tests/test_fetch_build_mpp.sh
bash tests/test_official_mpp_board.sh BUNDLE OUTPUT_DIR
bash tests/test_mpp_file_encoder.sh BUNDLE INPUT OUTPUT
bash tests/test_mpp_parameters.sh BUNDLE INPUT OUTPUT_DIR
bash tests/test_mpp_live_encoder.sh BUNDLE /dev/video11 OUTPUT
```

码流必须由官方 MPP decoder 和独立 FFmpeg 完整解码；文件非空不算成功。

Raw Annex-B 没有容器/RTP timestamps，FFmpeg 输入侧可能猜测 25 fps。MPP 编码
配置和 PTS 使用 30 fps；阶段 5 RTP/RTSP 必须显式提供时间基。
