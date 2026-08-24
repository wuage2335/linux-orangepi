# Orange Pi 5 Pro V4L2 Direct-MMAP RGA Comparison Design

日期：2026-08-24

## 1. 背景

阶段 3 已验证第一版实时 copy path：

```text
V4L2 MMAP -> DQBUF -> memcpy -> QBUF -> RGA -> 1280x720 NV12
```

300 帧实测为 30.04 fps、0 timeout、0 drop，显式 memcpy 平均 0.731 ms，
同步 RGA 平均 2.593 ms。下一项移除显式 memcpy，并以相同 3+300 帧契约比较
bypass、copy 和 direct 三条路径的吞吐及 CPU 占用，完成阶段 3 收口。

## 2. 目标

在现有 `rga_v4l2_live` 中增加向后兼容的 direct 模式：

```bash
# 现有 copy path
rga_v4l2_live /dev/video11 copy-last.nv12

# direct-MMAP path
rga_v4l2_live --direct /dev/video11 direct-last.nv12
```

Direct 模式在 STREAMON 前一次性把 4 个 V4L2 MMAP 地址导入 librga；每次
DQBUF 后，根据 buffer index 选择对应 RGA handle，同步 resize 完成后再 QBUF。

两种模式统一增加进程 user/system CPU 和 CPU 百分比统计，并与 RKISP bypass
路径形成三路径对比表。

## 3. 方案选择

采用“一个 binary、两个模式”，而不是新建重复程序：

- 两种模式共用完全相同的 V4L2 初始化、poll、sequence、统计和清理；
- 唯一变量是 source frame 交给 RGA 的方式，比较更公平；
- 已有两参数命令保持 copy 行为，现有测试和部署方式不失效；
- 避免复制数百行 ioctl/mmap/RAII 代码；
- 当前只有两个输入模式，不为此拆分公共静态库。

不采用每帧 import/release。它会把导入开销混入 RGA 计时，也不能代表长期实时
buffer 的实际用法。

不采用 DMA-BUF。DMA-BUF 还涉及 EXPBUF、fd 生命周期和缓存同步，应在后续
低延迟优化阶段独立验证。

## 4. CLI 与数据契约

参数解析：

```text
argc=3：copy，argv[1]=device，argv[2]=output
argc=4 且 argv[1]="--direct"：direct，argv[2]=device，argv[3]=output
其他形式：ERROR + 非零退出
```

两种模式共同固定：

- 输入：1920x1080 NV12 单平面，3,110,400 bytes；
- 输出：1280x720 NV12，1,382,400 bytes；
- V4L2: multiplanar MMAP；
- capture buffers: 4；
- pre-skip: 3；
- process: 300；
- poll timeout: 2000 ms；
- RGA: synchronous `imresize`；
- 输出只保存最后一帧。

成功日志增加 `mode=copy` 或 `mode=direct`。

## 5. VideoCapture 增量接口

`VideoCapture` 保持 fd、mapping 和 streaming 的唯一所有者，新增只读 view：

```cpp
struct MappedBufferView {
    void *address;
    std::size_t length;
};

std::vector<MappedBufferView> mapped_views() const;
```

该接口只在 STREAMON 前交给 direct resizer 建立 handles。它不转移 mmap 所有权，
不允许调用者 munmap，也不改变 QBUF/DQBUF 所有权规则。

`VideoCapture` 的生命周期必须长于 direct resizer，确保所有 RGA source handles
释放前 MMAP 地址仍有效。在 main 中先构造 capture，再构造 resizer；栈析构按
相反顺序先释放 RGA handles，再 munmap。

## 6. RGA 组件

### 6.1 CopyRgaResizer

由现有 `RgaResizer` 改名而来，行为不变：

- 一个独立 3,110,400-byte source vector；
- 一个 1,382,400-byte output vector；
- 两个长期 RGA handles；
- `source_data()` 供 memcpy；
- `resize()` 使用独立 source。

### 6.2 DirectRgaResizer

构造函数接受 4 个 `MappedBufferView`：

1. 验证每个 view 非空且 length 至少 3,110,400；
2. 每个地址执行一次 `importbuffer_virtualaddr`；
3. 每个 handle 用 1920x1080 NV12 包装为 `rga_buffer_t`；
4. 分配并导入一个共同的 1280x720 output vector；
5. 对每个 source buffer 与 output 执行 `imcheck`。

运行接口：

```cpp
void resize(unsigned int capture_index);
```

index 必须小于 4 个 source buffers。同步 `imresize` 返回前，main 不得 QBUF
该 capture index。

## 7. 两种主循环顺序

Copy：

```text
DQBUF(index)
-> memcpy 到 CopyRgaResizer source
-> QBUF(index)
-> copy_resizer.resize()
```

Direct：

```text
DQBUF(index)
-> direct_resizer.resize(index)
-> QBUF(index)
```

Copy 的 QBUF 在 RGA 前，因为 RGA 不再访问 MMAP 地址。Direct 的 QBUF 必须在
RGA 后，因为 RGA source 就是该 MMAP buffer。两者都使用同步 RGA，避免 fence
管理进入本阶段。

预丢弃帧在两种模式下都只 DQBUF/QBUF，不执行 RGA。

## 8. CPU 与耗时统计

正式循环开始和结束分别调用：

```cpp
getrusage(RUSAGE_SELF, &usage)
```

将 `ru_utime` 和 `ru_stime` 转换为毫秒：

```text
cpu_user_ms = user_after - user_before
cpu_system_ms = system_after - system_before
process_cpu_percent =
    (cpu_user_ms + cpu_system_ms) / (loop_total_s * 1000) * 100
```

该百分比表示当前进程平均占用，一个完整 CPU core 为 100%。RGA 硬件执行时间
不等于进程 CPU 时间；阻塞等待硬件期间不持续占用 CPU。

统一日志字段：

```text
mode=copy|direct
pre_skipped=3 processed=300 timeouts=0 dropped=0
copy_total_us=... copy_average_us=...
rga_total_us=... rga_average_us=...
loop_total_s=... capture_process_fps=...
cpu_user_ms=... cpu_system_ms=... process_cpu_percent=...
RGA_V4L2_LIVE_OK
```

Direct 的 copy 字段固定为 0.00，保持输出 schema 一致。

## 9. 外部 benchmark

新增板端脚本顺序运行：

1. RKISP bypass：`v4l2-ctl` 采集 300 帧到 `/dev/null`；
2. copy 模式 300 帧；
3. direct 模式 300 帧。

每条命令由 `/usr/bin/time -v` 包装，记录：

- User time；
- System time；
- Percent of CPU；
- Elapsed time；
- Maximum resident set size。

Copy/direct 以内置 getrusage 的正式循环数据为主要比较口径；外部 time 包含
初始化、预丢弃和退出，用作进程整体旁证。Bypass 只有外部 time 和 v4l2-ctl
帧率。

三次运行前只需统一执行一次 RKISP 配置；每个程序正常 STREAMOFF 后格式保持。

## 10. 错误处理

Direct 新增错误：

- `--direct` 参数不完整或多余；
- mapped view 数量与 capture buffers 不一致；
- view address 无效或 length 太小；
- 任一 source import 失败；
- 任一 source `imcheck` 失败；
- DQBUF index 超出 direct source vector；
- getrusage 失败。

所有错误打印 `ERROR:` 并返回非零。Direct 模式异常时栈展开顺序必须先释放
RGA handles，再 STREAMOFF/munmap/close。失败不得留下输出文件。

## 11. 测试

### 11.1 回归测试

现有 copy 命令和 `test_rga_v4l2_live.sh` 必须继续通过，并增加 CPU 字段检查。

### 11.2 Direct 黑盒测试

- `--direct` 无参数必须失败；
- `/dev/null` 必须失败；
- 正向处理 300 帧；
- mode=direct；
- timeout/drop 为 0；
- copy_average_us=0.00；
- CPU、RGA 和 FPS 字段完整；
- 最后一帧大小和 Y/UV 范围有效；
- PM 回到 suspended/usage 0；
- 无新增 CIF/ISP/RGA/MMU/IOMMU fault、timeout 或 overflow。

### 11.3 比较完成标准

形成 bypass/copy/direct 表格，至少包含：

```text
FPS
process CPU percent
user/system CPU time
copy average
RGA average
maximum RSS
timeout/drop
```

不预设 direct 一定更快。若 direct import 受驱动映射或缓存行为影响，按实测
报告，不为得到预期结论修改统计口径。

## 12. 阶段 3 收口

三路径比较通过后：

- 更新 `docs/codex/task_plan.md`，阶段 3 标记完成；
- 更新 HANDOFF 和新增 direct comparison 验证文档；
- 明确旋转/色彩转换当前无业务需求，因此未实现；
- 明确 DMA-BUF 属于后续低延迟优化；
- 下一阶段进入 MPP H.264/H.265 硬件编码。
