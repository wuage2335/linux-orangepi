# RK3588摄像头项目数据流、Buffer与拷贝分析

## 1. 结论

当前最终低延迟主链路是：

```text
OV13850 RAW10
-> MIPI D-PHY / CSI-2 / CIF online
-> RKISP
-> V4L2 DMA-BUF
-> MPP直接导入同一DMA-BUF
-> H.264 packet
-> GstBuffer
-> RTP/UDP或RTSP
-> Wi-Fi
-> Windows GStreamer
-> D3D11硬件解码与显示
```

主链路已移除的是：

```text
V4L2 1920x1080 NV12
-> CPU memcpy 3,110,400 bytes
-> MPP内部buffer
```

但“DMA-BUF零拷贝”只表示没有CPU整帧`memcpy`。RKISP、RGA、MPP、Wi-Fi和GPU
仍会通过DMA、片上总线或设备内存读写数据。

当前生产1080p链路绕过RGA。RGA代码只是独立resize实验，尚未形成
`RGA输出DMA-BUF -> MPP`的集成路径。

## 2. 三种容易混淆的操作

### 2.1 CPU拷贝

CPU执行类似：

```cpp
memcpy(destination, source, bytes);
```

CPU从一块内存读取数据，再写到另一块内存。它消耗CPU时间，并额外产生DDR读写。

### 2.2 DMA硬件读写

RKISP、RGA、MPP、网卡或GPU直接读写DDR或设备内存，不由CPU逐字节搬运。
它不会表现成CPU `memcpy`，但仍然占用DDR带宽。

### 2.3 映射和所有权传递

`mmap()`、`VIDIOC_EXPBUF`、`mpp_buffer_import()`通常不会复制像素。它们建立：

```text
用户虚拟地址
dma-buf fd
IOMMU映射
设备可访问句柄
```

这些不同的标识可以指向同一块底层buffer。

## 3. Sensor到RKISP

### 3.1 OV13850内部

```text
光线 -> 像素电荷 -> ADC -> 2112x1568 RAW10
```

数据在Sensor内部生成，不存在Linux CPU拷贝。OV13850将RAW10打包成MIPI CSI-2
packet并从两条Lane发送。

```text
CPU拷贝：无
数据移动：Sensor内部读出和MIPI串行发送
```

### 3.2 D-PHY与CSI-2

```text
MIPI差分信号
-> D-PHY恢复时钟和位流
-> CSI-2识别帧、Virtual Channel和RAW10 packet
-> CRC/ECC检查
```

这是硬件流式传输，数据经过片上FIFO和总线，没有用户态参与，也没有CPU整帧复制。

### 3.3 CIF到RKISP online

项目通过`normal_no_read_back=1`强制单摄online模式：

```text
CIF -> RKISP
```

没有采用：

```text
CIF -> RAW完整帧写DDR -> RKISP重新从DDR读取
```

因此online模式避免了RAW中间帧的DDR write/readback。CIF和RKISP按行流水运行，
不是先等待CIF完成整帧再启动ISP。

## 4. RKISP输出到V4L2 Buffer

RKISP输入为`2112x1568 SBGGR10`，执行裁剪、BLC、Demosaic、降噪、AWB gain、
CCM、Gamma、YUV转换和缩放，输出：

```text
1920x1080 NV12
Y  = 2,073,600 bytes
UV = 1,036,800 bytes
总计3,110,400 bytes/frame
```

RKISP通过DMA把NV12写进V4L2驱动buffer：

```text
CPU memcpy：无
ISP DMA写DDR：约3.11MB/frame
```

30fps时，仅该方向约为：

```text
3,110,400 x 30 = 93.3MB/s
```

这是硬件DMA写入，不应描述成CPU copy。

## 5. V4L2 Buffer池和用户态映射

### 5.1 `REQBUFS`与`QUERYBUF`

`V4L2Capture`请求4个驱动管理的buffer：

```text
VIDIOC_REQBUFS
VIDIOC_QUERYBUF
```

代码：`ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp`中的
`V4L2Capture::initialize()`。

### 5.2 `mmap`

```cpp
mmap(nullptr, plane_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
```

`mmap`只是让用户虚拟地址映射同一批底层页：

```text
驱动DMA buffer <-> 用户虚拟地址
```

没有复制3.11MB像素。

### 5.3 `QBUF`与`DQBUF`

```text
QBUF  -> buffer归驱动/RKISP，应用不得访问
DQBUF -> buffer归应用，驱动不得覆盖
```

生命周期：

```text
应用QBUF
-> RKISP DMA写入
-> vb2_buffer_done
-> 应用DQBUF
-> MPP读取
-> 应用重新QBUF
```

QBUF/DQBUF改变所有权，不复制像素。

## 6. V4L2到MPP的两种路径

### 6.1 Copy路径

copy模式调用：

```cpp
encoder.load_nv12(frame.data, kInputSize);
```

随后进入：

```cpp
copy_nv12_to_strided(destination, source, ver_stride);
```

数据流：

```text
V4L2 MMAP buffer
-> CPU读取3.11MB
-> CPU复制并调整stride
-> MPP内部buffer
```

这是明确的CPU整帧拷贝。实测：

```text
Mean 1.936ms
P50  2.030ms
P95  2.089ms
CPU  8.2%
```

一次3.11MB copy产生约6.22MB DDR流量：读3.11MB、写3.11MB；30fps额外约
186.6MB/s。

### 6.2 DMA-BUF路径

`V4L2Capture::initialize()`执行：

```cpp
VIDIOC_EXPBUF

MppBufferInfo info = {};
info.type = MPP_BUFFER_TYPE_EXT_DMA;
info.fd = export_buffer.fd;
info.size = plane_length;
mpp_buffer_import(&stored.mpp_buffer, &info);
```

同一块底层内存具有多种身份：

```text
V4L2 buffer index
CPU mmap地址
dma-buf fd
MPP MppBuffer
```

实时sender调用：

```cpp
encoder.encode_external_frame(
    capture.mpp_buffer(frame.index), ...);
```

MPP内部通过：

```cpp
mpp_frame_set_buffer(frame, input_buffer);
```

直接借用V4L2导出的buffer，没有调用`load_nv12()`或`memcpy()`。

```text
CPU整帧拷贝：无
MPP DMA读DDR：有，约3.11MB/frame
```

实测：

```text
copy路径CPU       8.2%
DMA-BUF路径CPU    3.2%
copy post-DQ平均  6.872ms
DMA post-DQ平均   5.230ms
```

## 7. RGA实验路径

当前1080p主链路的RKISP输出已经满足MPP，因此绕过RGA：

```text
RKISP -> V4L2 DMA-BUF -> MPP
```

### 7.1 RGA copy输入

```text
V4L2 MMAP
-> CPU memcpy到source_
-> RGA读取source_
-> RGA写1280x720 output_
```

CPU copy平均约1.253ms；RGA同步`imresize()`平均约2.721ms。

### 7.2 RGA direct-MMAP输入

```text
V4L2 MMAP地址
-> importbuffer_virtualaddr
-> RGA直接读取
-> RGA写独立output_
```

它移除了输入CPU copy，但RGA仍需读输入DDR并写输出DDR，平均约2.901ms。

### 7.3 当前RGA没有接入MPP

`rga_v4l2_live.cpp`中两种resizer的输出均为：

```cpp
std::vector<unsigned char> output_;
```

最后调用：

```cpp
write_output(output_path, resizer->output_data());
```

RGA代码没有`MppBuffer`、`MPP_BUFFER_TYPE_EXT_DMA`、`mpp_buffer_import`或
`encode_external_frame`；MPP/streaming代码也没有`imresize`或librga调用。

因此当前代码只有：

```text
路径A：RKISP -> V4L2 DMA-BUF -> MPP -> GStreamer
路径B：RKISP -> V4L2 MMAP -> RGA -> std::vector -> NV12文件
```

并没有：

```text
RKISP -> RGA -> MPP
```

如果直接把当前RGA `std::vector`输出交给`load_nv12()`，会发生新的CPU整帧copy。

### 7.4 真正的RGA到MPP DMA-BUF方案

未来应将RGA destination改成DMA-BUF：

```text
分配DMA-BUF
-> librga按fd导入为dst
-> RGA写入该DMA-BUF
-> MPP按同一fd导入MppBuffer
-> encode_external_frame()
```

这样无CPU整帧copy，但仍有RGA读输入DDR、RGA写输出DDR、MPP读输出DDR。

## 8. MPP编码过程

MPP收到NV12 DMA-BUF后创建`MppFrame`，设置宽高、stride、格式、PTS和buffer，
然后调用：

```text
encode_put_frame
-> RKVENC读取NV12
-> RKVENC写H.264 packet buffer
-> encode_get_packet
```

RKVENC硬件读取DDR并写压缩输出，不进行CPU整帧copy。约8Mbps、30fps时，压缩数据
平均约33KB/frame，而未压缩NV12是3.11MB/frame。

`EncodedPacketView`只保存packet地址、大小、PTS和关键帧标志，是非拥有型视图，
自身不复制数据。

## 9. MPP到GStreamer

`gst_rtp_sink.cpp`中的`make_gst_buffer()`执行：

```cpp
gst_buffer_new_allocate(nullptr, packet.size, nullptr);
gst_buffer_fill(buffer, 0, packet.data, packet.size);
```

这里有一次明确CPU copy：

```text
MPP packet buffer -> GstBuffer
```

但复制的是约几十KB的压缩H.264，而不是3.11MB NV12。真实约8Mbps码流实测：

```text
Mean 66.45us
P50  60.67us
P95  89.83us
```

`gst_app_src_push_buffer()`之后主要是GstBuffer所有权和引用传递。

## 10. GStreamer内部和Linux网络栈

### 10.1 `queue`与`h264parse`

`queue`保存GstBuffer引用；`h264parse`解析NAL、SPS/PPS、IDR和Access Unit。
它们可能创建新的buffer/view或重组压缩数据，但不会复制完整NV12图像。

### 10.2 `rtph264pay`

把H.264 NAL切成MTU约1200字节的RTP packet，并添加RTP头。可能有压缩payload
小块重组，不存在YUV整帧copy。

### 10.3 `udpsink`到socket

普通UDP发送通常会把用户态RTP payload复制到Linux socket/sk_buff：

```text
用户态GstBuffer -> Linux内核socket buffer
```

项目没有启用`MSG_ZEROCOPY`，所以这一段不能称为零拷贝。复制单位是约1200字节
RTP packet，而不是3.11MB帧。

### 10.4 bcmdhd与Wi-Fi

数据从skb进入bcmdhd、SDIO/总线和Wi-Fi固件队列。这里可能存在skb整理、总线buffer
和硬件DMA，是驱动/硬件数据移动，不是应用程序显式`memcpy`。

## 11. Windows接收、解码与显示

### 11.1 网络到GStreamer

```text
Wi-Fi网卡
-> Windows驱动/网络栈
-> udpsrc或rtspsrc GstBuffer
```

通常存在压缩RTP packet从内核到用户态的复制或buffer封装。

### 11.2 jitter buffer

`rtpjitterbuffer`保存和重排GstBuffer引用，等待迟到包并丢弃超过30ms窗口的数据。
它不处理未压缩图像。

### 11.3 depay与parse

`rtph264depay -> h264parse`去掉RTP头并重组H.264 Access Unit。可能发生压缩数据
拼接，但数据仍是H.264小块。

### 11.4 D3D11解码与显示

```text
H.264 GstBuffer
-> d3d11h264dec
-> GPU NV12/RGB surface
-> d3d11videosink
-> DWM/Present
```

GPU会写解码surface，内部也可能发生texture copy或颜色转换，但正常路径不会把
完整解码帧读回CPU。

## 12. 3A控制旁路

3A不承载完整视频帧：

```text
RKISP小型stats buffer
-> RKAIQ读取网格/直方图
-> CPU计算AE/AWB
-> V4L2 control小结构
-> I2C写几个Sensor寄存器
```

stats通过DMA写入，但远小于3.11MB图像。V4L2 ioctl会复制少量控制结构，I2C只发送
曝光、增益、VTS等几个字节。它们不是主视频数据的整帧copy。

## 13. 完整拷贝清单

| 阶段 | CPU整帧拷贝 | 硬件/DDR移动 | 说明 |
| --- | --- | --- | --- |
| Sensor到D-PHY | 无 | Sensor/MIPI | RAW10串行输出 |
| D-PHY到CSI/CIF | 无 | 片上流/FIFO | online |
| CIF到RKISP | 无 | 片上流 | 不落RAW中间帧 |
| RKISP到V4L2 | 无 | ISP DMA写DDR | 3.11MB/frame |
| V4L2 `mmap` | 无 | 页表映射 | 同一内存 |
| V4L2 `EXPBUF` | 无 | 导出fd/IOMMU | 同一内存 |
| Copy模式到MPP | 有 | CPU读写DDR | 3.11MB，P50约2.03ms |
| DMA-BUF到MPP | 无 | MPP DMA读DDR | 推荐路径 |
| RGA copy输入 | 有 | CPU读写DDR | 平均约1.25ms |
| RGA direct输入 | 无 | RGA读输入、写输出 | 仍有DDR流量 |
| MPP编码 | 无整帧CPU copy | RKVENC读写DDR | 产生H.264 |
| MPP packet到GstBuffer | 有，小块 | CPU复制压缩数据 | P50约60.67us |
| GStreamer到socket | 有，小块 | 用户到内核复制 | RTP约1200B/packet |
| Wi-Fi发送/接收 | 无应用层copy | 驱动/硬件传输 | skb/SDIO/DMA |
| Windows socket到GStreamer | 可能有小块 | 内核到用户态 | 压缩RTP |
| H.264到D3D11 | 无CPU YUV整帧copy | GPU解码写surface | GPU路径 |
| D3D11到屏幕 | 无CPU整帧copy | GPU合成/Present | 低延迟显示 |

## 14. 粗略DDR流量比较

忽略对齐、cache、内部tile和协议开销，仅按1920x1080 NV12估算：

### DMA-BUF绕过RGA

```text
RKISP写NV12     约93.3MB/s
MPP读NV12       约93.3MB/s
H.264写出       约1MB/s
合计至少        约187.6MB/s
```

### CPU copy路径

额外增加：

```text
CPU读V4L2       约93.3MB/s
CPU写MPP buffer 约93.3MB/s
额外            约186.6MB/s
```

总量粗略提高到约374MB/s。该估算只用于解释copy代价，不替代DDR性能计数器实测。

## 15. 直接代码证据

### 当前DMA-BUF主链路

- `mpp/src/v4l2_capture.hpp`：`VIDIOC_EXPBUF`、`MPP_BUFFER_TYPE_EXT_DMA`、
  `mpp_buffer_import()`。
- `streaming/src/v4l2_mpp_rtp_sender.cpp`：DMA模式调用
  `encode_external_frame(capture.mpp_buffer(frame.index), ...)`。
- `streaming/src/v4l2_mpp_rtsp_server.cpp`：RTSP使用相同external buffer路径。
- `mpp/src/mpp_encoder_core.hpp`：`mpp_frame_set_buffer(frame, input_buffer)`。

### CPU copy对照路径

- `mpp/src/v4l2_mpp_encoder.cpp`：copy模式调用`encoder.load_nv12(frame.data, ...)`。
- `mpp/src/mpp_encoder_core.hpp`：`copy_nv12_to_strided()`执行Y/UV复制。

### RGA尚未接MPP的证据

- `rga/src/rga_v4l2_live.cpp`：输出是`std::vector<unsigned char> output_`并由
  `write_output()`保存文件。
- RGA源码没有MPP类型或调用；MPP/streaming源码没有librga或`imresize()`调用。

## 16. 面试回答建议

> 当前项目的1080p正式链路绕过RGA。RKISP把NV12通过DMA写入V4L2 buffer，应用
> 使用`VIDIOC_EXPBUF`导出dma-buf fd，再以`MPP_BUFFER_TYPE_EXT_DMA`导入MPP，
> 调用`encode_external_frame()`，所以V4L2到MPP没有CPU整帧copy。对照copy模式
> 会调用`load_nv12()`复制3.11MB，P50约2.03ms，CPU从DMA路径约3.2%升到8.2%。
> RGA当前只是独立resize实验，输出为`std::vector`并写文件，没有接入MPP，不能
> 声称已经完成RGA到MPP的DMA-BUF共享。MPP之后仍有一次压缩packet到GstBuffer的
> 小块CPU copy，真实8Mbps码流P50约60.67us；UDP socket、网络驱动和GPU内部也
> 存在数据移动，所以“零拷贝”只指移除了CPU的NV12整帧复制。
