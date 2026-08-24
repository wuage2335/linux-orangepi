# V4L2 Real-Time MPP Encoding Validation

日期：2026-08-25

## Copy Path Dynamic Scene

```text
pre_skipped=3 frames_in=300 frames_out=300
timeouts=0 dropped=0
packets=300 idr_frames=5
encoded_bytes=3815189
copy_average_us=2027.90
mpp_average_us=5122.12
loop_fps=30.03
SHA-256=bab53407dca430077af3838e2be8a16836fe43e91d098da513ef8b3806d1335f
```

实际约 9.16 Mbps，接近 8 Mbps CBR 的允许波动。官方 MPP decoder 和 FFmpeg
均完整解码 300 帧；PM 为 suspended/usage 0，无新增 fault。

## Deterministic Color-Bar Copy vs DMA-BUF

| Path | FPS | Copy avg | MPP avg | Process CPU | Max RSS | Encoded bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Copy | 30.03 | 1,824.80 us | 4,458.99 us | 8% | 18,428 KB | 29,774 |
| DMA-BUF | 30.03 | 0 | 4,836.12 us | 3% | 18,424 KB | 29,774 |

两条 color-bar 码流 SHA 完全相同：

```text
f69d33d682da84db0bbf06b00da11ca62d58045af684730f754e0d0b0b0050a9
```

这证明 DMA-BUF path 的 frame layout、cache visibility、buffer ownership 和
编码结果与 copy path 一致。DMA-BUF 移除约 1.82 ms copy，并降低进程 CPU；MPP
硬件时间在两条路径中接近。

## Ownership

Copy：

```text
DQBUF -> copy to internal MppBuffer -> QBUF -> encode internal buffer
```

DMA-BUF：

```text
EXPBUF/import once
DQBUF -> encode imported MppBuffer synchronously -> QBUF
```

DMA-BUF 模式必须在 MPP 返回 packet 后再 QBUF。V4L2 UV offset 对应
`ver_stride=1080`；使用 1088 会从错误 offset 读取色度。

## Result

实时 H.264 copy 和 DMA-BUF 均完成 300 帧、约30fps、0 timeout/drop，输出可
解码，PM 和内核日志正常。
