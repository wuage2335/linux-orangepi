# CPU有效内存带宽实验

<!-- camera-status-navigation -->
> 文档同步：2026-09-16。历史数据按原测试条件保留；当前阶段、环境、结果与待办统一见状态入口。
> 最新入口：[Camera 当前状态](../../../docs/codex/CURRENT_STATUS.md)。
<!-- /camera-status-navigation -->

完整方法与限制见仓库`docs/codex/ddr_cpu_bandwidth_validation.md`。
请先确认当前CPU负载、频率和温度；2026-09-16原始结果带有CPU0 TASKLET高负载背景。

在aarch64板端本目录执行：

```bash
git clone https://github.com/ssvb/tinymembench.git
git -C tinymembench checkout --detach a2cf6d7e382e3aea1eb39173174d9fa28cad15f3
make -C tinymembench CFLAGS="-DMAXREPEATS=3 -DLATBENCH_COUNT=1000000"
gcc -O3 -Wall -Wextra -Werror -std=gnu11 ddr_stream.c -o ddr_stream
python3 run_ddr.py
python3 analyze.py
```

runner默认创建新的`results/`目录，已存在时拒绝覆盖。绑定CPU3、CPU7及CPU4–7，
适用于本板已确认的大小核编号；更换硬件需要重新核对绑定。内存最多约512MiB，
SoC达80°C或进程超时会中止。读回校验是限定缓冲区的模式校验，不是全容量memtest。

复制吞吐按payload N字节计算。四核聚合值使用总payload除以共同测量窗口。
结果不是控制器物理总线字节计数，也不直接代表DMA-BUF映射的访存性能。
