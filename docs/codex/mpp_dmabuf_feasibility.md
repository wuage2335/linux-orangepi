# MPP DMA-BUF Feasibility

日期：2026-08-25

## EXPBUF Evidence

`/dev/video11` 的四个 MMAP index 均支持 `VIDIOC_EXPBUF`：

```text
index=0..3
size=3133440
V4L2_EXPBUF_OK buffers=4
```

导出大小等于 MPP 1920x1088 allocation size，但有效 NV12 layout 仍是 1080 行 Y
后紧跟 UV；allocation size 相同不代表 ver_stride 相同。

## MPP Import

每个 fd 使用：

```text
type=MPP_BUFFER_TYPE_EXT_DMA
fd=EXPBUF fd
size=3133440
```

在 STREAMON 前 import 一次。程序析构时先 `mpp_buffer_put`，再 close export fd，
最后 munmap/close video node。

## Layout Bug And Fix

首次原型把 DMA frame 配置为 ver_stride=1088，虽然编码返回成功，但码流显著偏小。
V4L2 实际 UV offset 是 `1920*1080`，MPP 却按 `1920*1088` 读取。修复 DMA
context/frame 为 ver_stride=1080 后，color-bar copy/dmabuf 码流 hash 完全相同。

## Conclusion

当前 RKISP/MppService 支持 V4L2 EXPBUF 到 MPP EXT_DMA 的低拷贝路径。它已通过
300 帧、确定性图像、码流 hash、解码、PM 和 fault 验证，可作为后续低延迟链路
的编码输入基线。
