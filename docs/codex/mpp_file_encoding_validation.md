# RK MPP File Encoding Validation

日期：2026-08-25

## Baseline

- Input: actual RKISP 1920x1080 NV12, 3,110,400 bytes
- Strided MppBuffer: 1920x1088, 3,133,440 bytes
- MPP: official 1.1.0, commit `c08762ebf`
- H.264 default: CBR 8 Mbps, 30 fps, GOP60, B=0

## Custom H.264 300 Frames

```text
frames_in=300 frames_out=300
packets=300 idr_frames=5
encoded_bytes=37039
elapsed=1.16 s encode_fps=257.62
SHA-256=39cc7da9efd03ebdfa01eba460467ef3f441f72c72c2969feb50d7e207c92978
```

输入为同一暗场单帧循环，内容接近静态，因此实际码流远低于目标 CBR；它用于
验证 MPI/packet/EOS，不用于码率收敛结论。

官方 `mpi_dec_test` 和 portable FFmpeg 7.0.2 均完整解码 300 帧，无 error。

## Parameter Matrix

| Codec/RC | Bitrate | GOP | Frames | IDR | SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| H.264 CBR | 4 Mbps | 30 | 30 | 2（含请求 IDR） | `4a90b164...56065` |
| H.264 VBR | 12 Mbps | 60 | 30 | 1 | `42c68430...d4f1` |
| H.265 CBR | 8 Mbps | 30 | 30 | 1 | `b33bc50c...c29d5` |

三条码流均由官方 decoder 和 FFmpeg 完整解码 30 帧。H.264 为 High Profile，
H.265 为 Main Profile，分辨率均为 1920x1080。

## Timing Boundary

MPP 配置 `rc:fps_in/out=30/1`，frame PTS 按 1/30 秒递增。Raw Annex-B 缺少容器
时间戳，FFmpeg 输入侧仍可能显示 25 fps/60 tbr；强制解码输出按 30 fps 得到
300帧/10秒。阶段 5必须由 RTP/容器 timestamps 明确帧率。

## Result

H.264/H.265 文件编码、CBR/VBR、GOP 和运行中 IDR 请求均通过。输出可完整解码，
无新增 MPP/RKVENC/IOMMU fault。
