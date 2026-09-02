# RK3588 Camera Pipeline Stage Timing Design

## Goal

Measure reproducible per-stage latency for the Orange Pi 5 Pro + OV13850
1080p30 pipeline. Every reported number must name its start event, end event,
clock source, workload, and whether frame waiting or queueing is included.

## Fixed Workloads

1. Deterministic: sensor test pattern 1, fixed exposure/gain/VBLANK, 3A off.
2. Real scene: normal indoor scene, RKAIQ AE/AWB on.
3. Warm up 30 frames, collect 300 frames, repeat five runs.
4. H.264 CBR 8 Mbps, GOP 30, four V4L2 buffers.
5. Report mean/min/max/P50/P95/P99 and keep per-frame CSV evidence.

## Metrics

| Metric | Start | End | Meaning |
| --- | --- | --- | --- |
| frame interval | prior V4L2 SOF timestamp | current SOF timestamp | true cadence, not processing |
| ISP output delay | RKISP SOF | mainpath buffer done | kernel-observed sensor-readout/ISP/DMA scope |
| SOF to userspace | V4L2 buffer timestamp | DQBUF return | ISP plus vb2 wakeup/scheduling |
| DQBUF wait | call dequeue | DQBUF return | frame wait and queue availability |
| copy | before load_nv12 | after load_nv12 | 3,110,400-byte CPU copy/padding |
| RGA | before imresize | after synchronous imresize | 1920x1080 to 1280x720 hardware resize |
| MPP encode | before encode call | packet returned | frame setup, hardware encode, packet retrieval |
| GStreamer push | sink consume entry | appsrc push return | compressed packet copy and enqueue only |
| requeue | before QBUF | after QBUF | buffer ownership return |
| post-DQ pipeline | DQBUF return | QBUF after encode | userspace critical section |

RKISP `/proc/rkisp1-vir0` already exposes online frame time, v-blank, output
rate, output delay, frame loss, and buffer count. A sampler records each unique
frame without modifying the kernel.

## Instrumentation

- Extend `CapturedFrame` with the monotonic V4L2 timestamp.
- Add a tested percentile helper shared by benchmarks.
- Add `pipeline_stage_benchmark.cpp` with null and RTP sinks.
- Add per-call distribution output to `rga_v4l2_live.cpp`.
- Add a procfs sampler and a result aggregator.
- Do not change the validated sender or RTSP defaults.

## Measurement Boundaries

- DQBUF wait near 33.3 ms is the 30fps period, not ISP latency.
- RKISP output delay is SOF to completed mainpath buffer; it includes sensor
  readout overlap and DMA, so it is not described as pure ISP arithmetic time.
- GStreamer push return does not prove arrival at the PC.
- Network/decode/display remain part of optical same-screen end-to-end latency.
- RGA is optional and bypassed in the production 1080p path.

## Acceptance

- Host tests fail before and pass after the timing helper/timestamp work.
- Native board binaries build with `-Wall -Wextra -Werror`.
- Five runs complete without timeout, sequence drop, queue overrun, or new
  ISP/MPP/RGA/IOMMU faults.
- Raw CSV, command logs, environment, and summary are stored under one result
  directory and documented in `docs/codex`.
