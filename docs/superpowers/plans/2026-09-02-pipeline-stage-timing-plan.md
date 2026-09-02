# RK3588 Camera Pipeline Stage Timing Implementation Plan

**Goal:** Build, deploy, and run a reproducible per-stage latency benchmark.

## Tasks

- [x] Record board/kernel/camera/RKISP/MPP/RGA/GStreamer environment.
- [x] RED: add failing tests for percentile statistics and V4L2 timestamps.
- [x] GREEN: implement timing helper and capture timestamp propagation.
- [x] Add pipeline benchmark with null/RTP sinks and per-frame CSV.
- [x] Add RGA per-call percentile reporting.
- [x] Add RKISP procfs sampler and five-run aggregation workflow.
- [x] Build host contracts and native aarch64 binaries.
- [x] Deploy an isolated timing bundle without replacing system libraries.
- [x] Run deterministic copy, DMA-BUF, RGA and RTP timing matrix.
- [x] Run current-scene RKAIQ cross-check and collect CPU/RSS/temperature.
- [x] Inspect PM state and new kernel fault logs after every group.
- [x] Write quantitative results with measurement boundaries.
- [x] Commit and merge locally; leave push to the user.

Local merge commit: `7b02f82f6 merge: add camera pipeline timing benchmark`.

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
