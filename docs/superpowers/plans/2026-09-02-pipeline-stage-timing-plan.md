# RK3588 Camera Pipeline Stage Timing Implementation Plan

**Goal:** Build, deploy, and run a reproducible per-stage latency benchmark.

## Tasks

- [ ] Record board/kernel/camera/RKISP/MPP/RGA/GStreamer environment.
- [ ] RED: add failing tests for percentile statistics and V4L2 timestamps.
- [ ] GREEN: implement timing helper and capture timestamp propagation.
- [ ] Add pipeline benchmark with null/RTP sinks and per-frame CSV.
- [ ] Add RGA per-call percentile reporting.
- [ ] Add RKISP procfs sampler and five-run aggregator.
- [ ] Build host contracts and native aarch64 binaries.
- [ ] Deploy an isolated timing bundle without replacing system libraries.
- [ ] Run deterministic copy, DMA-BUF, RGA and RTP timing matrix.
- [ ] Run normal-scene RKAIQ cross-check and collect CPU/RSS/temperature.
- [ ] Inspect PM state and new kernel fault logs after every group.
- [ ] Write quantitative results with measurement boundaries.
- [ ] Commit and merge locally; leave push to the user.

## Expected Commands

```text
pipeline_stage_benchmark --mode copy|dmabuf --sink null|rtp
rga_v4l2_live /dev/video11 output.nv12
sample_rkisp_proc.py --frames 300 --output rkisp.csv
aggregate_pipeline_timing.py RESULT_DIR
```

## Known Limits

- No trace-cmd/perf package is installed on the board.
- Pure internal ISP block latency cannot be separated without new kernel trace
  points; procfs output delay is the supported SOF-to-mainpath scope.
- PC arrival and present timestamps are not clock-synchronised in this pass;
  optical end-to-end evidence remains the authoritative screen latency.
