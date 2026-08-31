# C/C++ 源文件零基础业务注释设计

## 1. 目标

为 Orange Pi 5 Pro + OV13850 摄像头项目中由项目成员新增或重点修改的非测试
`.c`、`.cpp` 文件补充中文业务注释，使没有 Linux 摄像头、V4L2、RGA、MPP、
GStreamer 或 RKAIQ 基础的读者，也能沿着数据流理解每个文件的责任。

本次只改变注释，不改变代码、接口、构建参数、日志或运行行为。

## 2. 范围

目标文件共 12 个：

1. `drivers/media/i2c/ov13850.c`
2. `drivers/media/i2c/ov13850_i2c_min.c`
3. `ov13850_opi5pro_learning/rga/src/rga_nv12_resize.cpp`
4. `ov13850_opi5pro_learning/rga/src/rga_v4l2_live.cpp`
5. `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`
6. `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`
7. `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.cpp`
8. `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp`
9. `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`
10. `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp`
11. `ov13850_opi5pro_learning/rkaiq/src/rkmodule_info_probe.c`
12. `ov13850_opi5pro_learning/rkaiq/src/rkmodule_info_preload.c`

明确排除测试文件、头文件、Shell/PowerShell、Makefile、DTS、第三方源码、生成物和
二进制文件。

## 3. 方案比较

### 方案 A：分层业务注释（采用）

在文件、核心类型、核心函数和关键状态转换处解释业务意义。优点是注释贴近代码，
读者可以顺着执行路径学习，又不会被每行重复说明淹没。

### 方案 B：逐行注释（不采用）

逐行解释赋值、循环和标准库调用。它看起来详细，但会扩大文件、重复代码字面含义，
真正重要的缓冲区所有权、生命周期和错误恢复反而更难找到。

### 方案 C：只写独立教程（不采用）

教程适合讲完整概念，但代码演进后容易与实现脱节。本项目已经有阶段文档，本次重点
是让源文件自身成为可阅读的学习材料。

## 4. 注释层次

### 4.1 文件头

每个文件开头说明：

- 它处于 `Sensor -> ISP -> RGA -> MPP -> RTP/RTSP -> 3A` 的哪个位置；
- 输入数据是什么，输出数据是什么；
- 为什么项目需要这个文件；
- 初学者阅读该文件时应先抓住哪条主线。

### 4.2 核心结构和状态

解释 V4L2 buffer、DMA-BUF fd、MPP frame/packet、GStreamer appsrc、RTSP client、
runtime PM、control、RKAIQ module-info 等对象的业务身份、所有者和生命周期。

### 4.3 核心函数

对入口函数和关键函数说明：调用者、前置条件、主要步骤、成功后产生的状态、失败时
如何清理。复杂流程前使用短步骤列表，不对显而易见的语法做逐行翻译。

### 4.4 风险点

重点解释以下容易产生真实故障的位置：

- sensor 上电、寄存器表、stream on/off 和 runtime PM 引用；
- NV12 stride、plane 大小和 crop/resize；
- MMAP 与 DMA-BUF 的所有权和 QBUF 时机；
- MPP 输入帧和输出 packet 的同步关系；
- RTP 时间戳、RTSP 实时时钟、断线重连和关键帧；
- module-info ioctl shim 的启用边界，避免拦截无关设备。

## 5. 注释规则

- 使用简体中文，首次出现的缩写同时给出英文全称或直白解释。
- 解释“为什么”和“业务后果”，不只复述函数名。
- 不承诺代码没有提供的行为，不把推测写成事实。
- 不修改既有 ABI、CLI、日志文本和控制流程。
- 保留已有正确注释；重复内容合并，而不是叠加第二套说法。
- 内核文件遵循现有 C 注释风格，用户态 C++ 遵循项目现有块注释风格。

## 6. 验证

1. `git diff --check` 确认无空白错误。
2. 使用去注释后的 token/预处理结果比较，确认只发生注释变化。
3. 构建两个内核驱动目标或执行对应语法检查。
4. 交叉编译 RGA、MPP、streaming 的目标源文件。
5. 运行 RKAIQ host tools/test 和现有非板端测试。
6. 检查每个目标文件都具备文件级业务说明，核心入口和关键所有权路径有解释。

## 7. 完成标准

- 12 个目标文件全部完成分层注释；
- 一个零基础读者能回答每个文件的输入、输出、职责和关键风险；
- 代码 token、构建结果和现有测试相对基线没有变化；
- 测试脚本及范围外文件没有被修改。
