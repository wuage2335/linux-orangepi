# 2026-09-16 DDR读写基准

## 结论与范围

本轮完成CPU访问内存的顺序读、写、复制及四大核并发吞吐测试，得到实际payload
MB/s。63组测量的读回校验均通过，公开tinymembench交叉检查正常退出。
这不是全SoC DDR控制器的总读写字节计数，也不是16GiB全容量内存故障检测。

## 环境

- Orange Pi 5 Pro，RK3588S，约16GiB内存；SSH 192.168.1.16。
- 内核6.1.99-opi5pro-livecfg-baseline；DFI时钟overlay已启用。
- DMC governor为dmc_ondemand，自动调频，未强制锁定最高频率。
- CPU3为A55，最高1.8GHz；CPU7为A76，最高2.304GHz；并发使用CPU4–7。
- 摄像头与RKAIQ未运行，sensor PM suspended。
- **存在后台干扰**：CPU0的ksoftirqd/0约97% CPU；3秒TASKLET计数增加19,529,492。
  测试避开CPU0，但仍共享内存系统。具体驱动尚未定位，不能仅凭出现时间归因DFI。

## 方法

自有`ddr_stream.c`使用NEON累加读取、libc memset写入和libc memcpy复制。
内存4096字节对齐，先初始化/预触页，再预热0.5秒；屏障防止编译器删除重复访存。
顺序读检验累计和，写逐字节读回，复制用memcmp校验。校验不计入单轮计时。

- 单核：每块缓冲区64MiB或256MiB；每轮至少2秒，重复3次。
- 四大核：每核两块64MiB缓冲区；每轮至少3秒，重复3批；最大总分配约512MiB。
- 多核总吞吐=总payload字节数/(最晚测量结束-最早测量开始)，并非单核峰值相加。
- 按1MB=1,000,000字节、1GB=1,000,000,000字节计算。复制按N字节payload计数，
  不把读N+写N翻倍后称为物理总线实测。
- 每250ms保存DMC频率/load、CPU频率和SoC温度。超时或SoC达80°C即中止。

## 实测结果

以下为三轮中位数，单位GB/s（十进制）：

| 条件 | 顺序读 | 顺序写 | 复制payload |
| --- | ---: | ---: | ---: |
| CPU7大核，每块64MiB | 18.371 | 27.271 | 12.540 |
| CPU7大核，每块256MiB | 18.147 | 26.817 | 10.877 |
| CPU3小核，每块64MiB | 5.997 | 21.607 | 6.131 |
| 四大核并发，每核每块64MiB | 22.802 | 26.009 | 10.558 |

四核并发没有线性提升；本轮复制总吞吐反而低于单大核64MiB结果。该现象与共享
内存资源竞争、实现和调频条件有关，现有数据不足以把差异归结为唯一硬件原因。
完整每轮MB/s、上下界、时间窗口见`results/analysis.json`与对应原始日志。

## 公开工具交叉检查

来源：https://github.com/ssvb/tinymembench

固定commit：`a2cf6d7e382e3aea1eb39173174d9fa28cad15f3`。
源码未改动；编译时使用工具自带参数`-DMAXREPEATS=3 -DLATBENCH_COUNT=1000000`，
绑定CPU7。默认吞吐缓冲区为32MiB，报告采用工具的最佳样本，与上表中位数口径不同。

- standard memcpy：13245.7MB/s。
- standard memset：27773.6MB/s。
- NEON STP fill：27772.8MB/s。
- 64MiB随机访问额外延迟：单链239.1ns、双链269.3ns；按工具定义是扣除基础开销后、
  需叠加L1访问延迟的指标，不是纯DRAM芯片时序。

## 温度、退出和局限

SoC采样峰值74.846°C，未触发80°C中止条件；结束后DMC回到534MHz、sensor PM
suspended，内核日志无新增记录。CPU0 TASKLET异常仍存在，本轮未修改内核进行处理。

本轮分配的是普通可缓存用户态内存，因此不能把其memcpy速度直接代替V4L2 MMAP/
DMA-BUF上的真实拷贝耗时；后者映射属性和流水线开销不同。DMC load仍仅表示最忙
通道busy/total利用率，本轮也没有由它推算全芯片绝对DDR流量。

如需作为无干扰硬件性能基线，应先定位CPU0 TASKLET异常，再以同样参数做对照。

## 复现与证据

板端目录：`/home/orangepi/ov13850_opi5pro_learning/release/ddr-20260916/`。
Windows目录：`C:/Users/Administrator/Documents/Camera开发/ddr-test-work/`。

```bash
gcc -O3 -Wall -Wextra -Werror -std=gnu11 ddr_stream.c -o ddr_stream
taskset -c 7 ./ddr_stream read 64 2 3
taskset -c 7 ./ddr_stream write 64 2 3
taskset -c 7 ./ddr_stream copy 64 2 3
```

完整矩阵由`run_ddr.py`执行，使用新的`results/`目录以防覆盖证据。
`results.tar.gz`保留原始记录与运行源码，`analyze.py`重算中位数和并发吞吐。
