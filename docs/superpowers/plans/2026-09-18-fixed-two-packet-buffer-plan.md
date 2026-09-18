# Fixed Two-Packet Buffer Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace per-frame MPP output-buffer allocation with a preallocated two-slot pool and verify whether two buffers sustain 1080p30 RTP/RTSP without pool misses.

**Architecture:** A host-testable `FixedSlotPool` owns slot availability and statistics. `MppEncoder` preallocates the configured number of `MppBuffer` objects once; a packet lease returns its slot when GStreamer releases the final reference. Pool exhaustion skips that encoded frame, increments a counter, and requests IDR before the next emitted frame.

**Tech Stack:** C++17, Rockchip MPP 1.1.0, GStreamer 1.20, GNU Make, Orange Pi 5 Pro.

---

### Task 1: Specify fixed two-slot behavior with a failing host test

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/src/fixed_slot_pool.hpp`
- Create: `ov13850_opi5pro_learning/streaming/tests/test_fixed_slot_pool.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/Makefile`

- [x] **Step 1: Add a test that acquires two slots and rejects the third**

```cpp
FixedSlotPool pool(2);
auto first = pool.try_acquire();
auto second = pool.try_acquire();
auto third = pool.try_acquire();
require(first && second, "two-slot pool did not provide two slots");
require(!third, "two-slot pool exceeded capacity");
require(pool.peak_in_flight() == 2, "peak mismatch");
require(pool.acquire_misses() == 1, "miss count mismatch");
```

- [x] **Step 2: Run `make test-fixed-slot-pool` and verify RED**

Expected: compilation fails because `fixed_slot_pool.hpp` does not exist.

- [x] **Step 3: Implement the minimal mutex-protected index pool**

The pool validates positive capacity, pops an index from `free_slots_`, tracks current/peak/misses, rejects duplicate or out-of-range release, and makes a released slot immediately reusable.

- [x] **Step 4: Run `make test-fixed-slot-pool` and verify GREEN**

Expected: `PASS: fixed slot pool`.

### Task 2: Preallocate MPP packet buffers and expose pool statistics

**Files:**
- Modify: `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp`
- Modify: `ov13850_opi5pro_learning/benchmarks/include/pipeline_benchmark_config.hpp`
- Modify: `ov13850_opi5pro_learning/benchmarks/src/pipeline_stage_benchmark.cpp`

- [x] **Step 1: Add `packet_buffers=2` to `EncoderConfig`**

Create `MppPacketBufferPool` during encoder initialization. It allocates exactly `packet_buffers` buffers from the existing MPP group and retains them until all leases are released.

- [x] **Step 2: Replace per-frame `ScopedMppBuffer` with pool acquisition**

If `try_acquire()` fails, increment `packet_pool_misses`, set `request_idr_after_pool_miss_`, and return without submitting the frame. On the next successful acquisition, request IDR before encoding.

- [x] **Step 3: Publish pool statistics**

Extend `EncoderStats` with capacity, peak in-flight, current in-flight, and misses. RTP/RTSP output must print these values. `frames_sent` counts emitted packets rather than input frames.

- [x] **Step 4: Add `--packet-buffers` to RTP, RTSP, and benchmark parsing**

Default to 2, validate range 1..64, pass it into `EncoderConfig`, and include it in usage/output.

### Task 3: Verify two-buffer behavior on host and board

**Files:**
- Modify: `docs/codex/camera_data_flow_and_copy_analysis.md`
- Modify: `docs/codex/progress.md`

- [x] **Step 1: Run all host builds and tests**

Run `build_host.sh`; require all streaming, benchmark, and Python tests to pass with `-Werror`.

- [x] **Step 2: Build natively on Orange Pi**

Run `build_board.sh`; require `BOARD_BUILD_AND_TESTS_OK`.

- [x] **Step 3: Run fixed-controls RTP and RTSP tests with `--packet-buffers 2`**

Require 30.04fps, zero capture timeout/drop/queue overrun, successful reconnect/decode, `packet_pool_misses=0`, and peak in-flight no greater than 2.

- [x] **Step 4: Run a deliberately slow-client stress test**

Record peak in-flight, misses, recovery IDR behavior, bounded memory, PM state, and kernel fault scan. If misses occur, report that two buffers are insufficient for that workload rather than increasing capacity silently.
