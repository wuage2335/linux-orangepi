# RK3588 Stage 5 Low-Latency Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. The user explicitly requested no subagents. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a reproducible 1920x1080@30 low-latency pipeline from OV13850/RKISP through V4L2 DMA-BUF and Rockchip MPP H.264 into RTP/UDP and RTSP, with Windows GStreamer playback and measured end-to-end latency no greater than 200 ms.

**Architecture:** Refactor the Stage 4 encoder so encoded packets are delivered through a synchronous typed sink instead of being tied to `std::ostream`. Keep the existing file sinks for regression, then add a native-board GStreamer `appsrc` bridge that assigns 30 fps PTS/DTS and lets `rtph264pay` generate RFC-compliant 90 kHz RTP timestamps. Validate RTP/UDP first; add a shared `gst-rtsp-server` media factory only after RTP timing and recovery are proven.

**Tech Stack:** Linux V4L2 multi-planar capture, DMA-BUF/EXPBUF, Rockchip MPP 1.1.0, C++17, GStreamer 1.x (`appsrc`, `h264parse`, `rtph264pay`, `rtpjitterbuffer`, `gst-rtsp-server`), Bash, PowerShell, Windows GStreamer, Wireshark/tshark where available.

---

## File Map

### Existing files to modify

- `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp`: replace ostream-only packet output with a synchronous packet sink contract.
- `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`: use the file sink without changing CLI or output behavior.
- `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`: consume shared V4L2 capture code and preserve copy/DMA-BUF Stage 4 behavior.
- `ov13850_opi5pro_learning/mpp/Makefile`: build new host-independent sink test and continue producing the Stage 4 bundle.
- `ov13850_opi5pro_learning/mpp/scripts/package_mpp_bundle.sh`: include official MPP headers needed for native board compilation.
- `ov13850_opi5pro_learning/mpp/tests/test_mpp_file_encoder.sh`: confirm refactor does not change file encoding contract.
- `ov13850_opi5pro_learning/mpp/tests/test_mpp_live_encoder.sh`: confirm copy/DMA-BUF regression after extraction.
- `docs/codex/README.md`, `HANDOFF.md`, `task_plan.md`, `progress.md`, `findings.md`: add Stage 5 source, commands, evidence, and current status.
- `docs/codex/orangepi5pro-kernel-troubleshooting.md` and learning-project copy: record streaming-specific failures and fixes.

### New MPP shared files

- `ov13850_opi5pro_learning/mpp/src/encoded_packet_sink.hpp`: standard-C++ packet view, sink interface, and ostream sink.
- `ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp`: reusable V4L2 MMAP/EXPBUF capture and buffer ownership.
- `ov13850_opi5pro_learning/mpp/tests/test_encoded_packet_sink.cpp`: host test for packet flags, timestamps, and synchronous consumption.
- `ov13850_opi5pro_learning/mpp/tests/check_v4l2_capture_compile.cpp`: ARM64 compile contract for the extracted capture class.

### New streaming project

- `ov13850_opi5pro_learning/streaming/.gitignore`: ignore native board build and deployment bundles.
- `ov13850_opi5pro_learning/streaming/Makefile`: native board build against bundled MPP and system GStreamer.
- `ov13850_opi5pro_learning/streaming/README.md`: build, RTP sender, Windows receiver, RTSP, and latency instructions.
- `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.hpp`: GStreamer appsrc RTP sink public contract.
- `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.cpp`: pipeline, GstBuffer timestamping, queue overrun, and bus handling.
- `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`: DMA-BUF capture, MPP encoding, RTP sender CLI, statistics, and signal handling.
- `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.hpp`: shared RTSP media factory contract.
- `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp`: RTSP appsrc lifecycle and client recovery.
- `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp`: capture/encoder worker plus GLib RTSP main loop.
- `ov13850_opi5pro_learning/streaming/scripts/check_gstreamer_board.sh`: board commands, plugins, headers, and MPP device inventory.
- `ov13850_opi5pro_learning/streaming/scripts/check_gstreamer_windows.ps1`: Windows plugin/decoder/sink inventory.
- `ov13850_opi5pro_learning/streaming/scripts/send_h264_file_rtp.sh`: Stage 4 file-to-RTP baseline.
- `ov13850_opi5pro_learning/streaming/scripts/receive_h264_rtp.ps1`: explicit Windows RTP receive pipeline.
- `ov13850_opi5pro_learning/streaming/scripts/package_streaming_source.sh`: source plus official MPP development bundle for board-native build.
- `ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh`: mocked command-line contract tests.
- `ov13850_opi5pro_learning/streaming/tests/test_gst_rtp_sink.cpp`: board-native appsrc/fakesink timestamp test.
- `ov13850_opi5pro_learning/streaming/tests/test_rtp_sender_board.sh`: live sender FPS/drop/PM/log acceptance.
- `ov13850_opi5pro_learning/streaming/tests/test_rtsp_recovery.sh`: RTSP connect, disconnect, and reconnect acceptance.
- `docs/codex/stage5_rtp_streaming_validation.md`: environment, RTP packet timing, live results, and latency evidence.
- `docs/codex/stage5_rtsp_recovery_validation.md`: RTSP session and recovery evidence.

---

### Task 1: Inventory Board and Windows GStreamer

**Files:**
- Create: `ov13850_opi5pro_learning/streaming/scripts/check_gstreamer_board.sh`
- Create: `ov13850_opi5pro_learning/streaming/scripts/check_gstreamer_windows.ps1`
- Create: `ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh`
- Create: `ov13850_opi5pro_learning/streaming/.gitignore`

- [ ] **Step 1: Write the failing shell contract test**

The test creates fake `gst-launch-1.0`, `gst-inspect-1.0`, `pkg-config`, and `g++` commands, runs the board checker, and asserts that all required plugin names are queried:

```bash
required='appsrc h264parse rtph264pay udpsink udpsrc rtpjitterbuffer rtph264depay'
for plugin in $required; do
    grep -F "gst-inspect-1.0 $plugin" "$CALL_LOG"
done
grep -F 'gstreamer-app-1.0' "$CALL_LOG"
grep -F 'gstreamer-rtsp-server-1.0' "$CALL_LOG"
```

- [ ] **Step 2: Run the test and verify the red state**

Run:

```bash
bash ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh
```

Expected: `FAIL: missing board environment checker`.

- [ ] **Step 3: Implement the board checker**

The script must print machine/kernel/GStreamer versions, test `/dev/mpp_service` and `/dev/video11`, query each required plugin, and check native development packages:

```bash
gst-launch-1.0 --version
for plugin in appsrc h264parse rtph264pay udpsink \
              udpsrc rtpjitterbuffer rtph264depay; do
    gst-inspect-1.0 "$plugin"
done
pkg-config --modversion gstreamer-1.0 gstreamer-app-1.0
pkg-config --modversion gstreamer-rtsp-server-1.0
```

Return nonzero and print the exact missing command, plugin, or pkg-config module.

- [ ] **Step 4: Implement the Windows checker**

PowerShell must query:

```powershell
gst-launch-1.0 --version
$plugins = @(
  'udpsrc', 'rtpjitterbuffer', 'rtph264depay', 'h264parse',
  'd3d11h264dec', 'avdec_h264', 'd3d11videosink', 'autovideosink'
)
foreach ($plugin in $plugins) {
    gst-inspect-1.0 $plugin
}
```

It must report hardware decoder availability separately from the software fallback.

- [ ] **Step 5: Run syntax and mocked tests**

Run:

```bash
bash -n ov13850_opi5pro_learning/streaming/scripts/*.sh
bash ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh
```

Expected: `PASS: streaming script contracts`.

- [ ] **Step 6: Run real inventories**

Codex runs the board checker over SSH. User cooperation is required only if Windows GStreamer is absent or the PowerShell checker needs to be run in the user session.

Record exact versions and missing packages before installing anything.

- [ ] **Step 7: Commit**

```bash
git add ov13850_opi5pro_learning/streaming
git commit -m "test(streaming): inventory gstreamer environments"
```

### Task 2: Establish File-to-RTP/UDP Baseline

**Files:**
- Create: `ov13850_opi5pro_learning/streaming/scripts/send_h264_file_rtp.sh`
- Create: `ov13850_opi5pro_learning/streaming/scripts/receive_h264_rtp.ps1`
- Modify: `ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh`
- Create: `docs/codex/stage5_rtp_streaming_validation.md`

- [ ] **Step 1: Extend the failing script test**

Assert that sender arguments include explicit H.264/RTP parameters:

```text
filesrc location=<input>
h264parse
video/x-h264,stream-format=byte-stream,framerate=30/1
rtph264pay pt=96 mtu=1200 config-interval=1
udpsink host=<host> port=<port> sync=true async=false
```

Assert that the receiver includes explicit caps:

```text
application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000
rtpjitterbuffer latency=30 drop-on-latency=true
rtph264depay
```

- [ ] **Step 2: Run the test and verify failure**

Expected: failure because sender and receiver scripts do not exist.

- [ ] **Step 3: Implement file sender and Windows receiver scripts**

Sender CLI:

```bash
send_h264_file_rtp.sh INPUT.h264 WINDOWS_IP [PORT]
```

Receiver CLI:

```powershell
./receive_h264_rtp.ps1 -Port 5004 -LatencyMs 30 -Decoder auto
```

The receiver selects `d3d11h264dec ! d3d11videosink` when available and otherwise uses `avdec_h264 ! autovideosink`.

- [ ] **Step 4: Run mocked and syntax tests**

Expected: `PASS: streaming script contracts`.

- [ ] **Step 5: Run board-to-Windows file RTP**

User cooperation: start the Windows receiver and confirm visible playback. Codex starts the sender and captures sender/receiver logs.

This step proves plugin/caps/network compatibility only; raw-file pacing and latency are not accepted as live timing evidence.

- [ ] **Step 6: Record evidence and commit**

```bash
git add ov13850_opi5pro_learning/streaming docs/codex/stage5_rtp_streaming_validation.md
git commit -m "feat(streaming): add h264 file rtp baseline"
```

### Task 3: Introduce a Typed MPP Packet Sink

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/src/encoded_packet_sink.hpp`
- Create: `ov13850_opi5pro_learning/mpp/tests/test_encoded_packet_sink.cpp`
- Modify: `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp`
- Modify: `ov13850_opi5pro_learning/mpp/src/nv12_mpp_encoder.cpp`
- Modify: `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`
- Modify: `ov13850_opi5pro_learning/mpp/Makefile`

- [ ] **Step 1: Write the host-side failing packet sink test**

Define the required public contract in the test:

```cpp
struct EncodedPacketView {
    const std::uint8_t *data;
    std::size_t size;
    std::int64_t pts_us;
    bool keyframe;
    bool codec_config;
    bool eos;
};

class EncodedPacketSink {
public:
    virtual ~EncodedPacketSink() = default;
    virtual void consume(const EncodedPacketView &packet) = 0;
};
```

Test `OstreamPacketSink` with header bytes and frame bytes, including embedded zeroes. Verify exact output bytes and that metadata is not silently changed.

- [ ] **Step 2: Run the host test and verify failure**

Run:

```bash
g++ -std=gnu++17 -Wall -Wextra -Werror \
  ov13850_opi5pro_learning/mpp/tests/test_encoded_packet_sink.cpp \
  -o /tmp/test_encoded_packet_sink
```

Expected: missing `encoded_packet_sink.hpp`.

- [ ] **Step 3: Implement the minimal packet sink header**

`OstreamPacketSink::consume()` writes exactly `packet.size` bytes and throws on stream failure. Document that `data` is borrowed and valid only during `consume()`.

- [ ] **Step 4: Refactor MppEncoder delivery**

Change APIs to:

```cpp
void write_header(EncodedPacketSink &sink, EncoderStats &stats);
bool encode_frame(int index, bool eos,
                  EncodedPacketSink &sink, EncoderStats &stats);
bool encode_external_frame(MppBuffer buffer, int index, bool eos,
                           EncodedPacketSink &sink, EncoderStats &stats);
```

Convert each `MppPacket` to `EncodedPacketView` before deinit. Use MPP packet/meta fields for EOS and keyframe; assign normal frame `pts_us=index*1000000/30`; mark `MPP_ENC_GET_HDR_SYNC` output as `codec_config=true` and `pts_us=-1`.

- [ ] **Step 5: Update existing frontends**

Construct `OstreamPacketSink output_sink(output)` in both Stage 4 programs. Keep CLIs, stats text, H.264/H.265 bytes, and cleanup behavior unchanged.

- [ ] **Step 6: Run host and ARM64 compile tests**

```bash
/tmp/test_encoded_packet_sink
make -C ov13850_opi5pro_learning/mpp clean bundle
```

Expected: `PASS: encoded packet sink`; `MPP_BUNDLE_OK`.

- [ ] **Step 7: Run Stage 4 board regressions**

Run file H.264, parameter matrix, live copy, and live DMA-BUF tests. Require complete decoder frame counts, 0 timeout/drop, PM suspended/0, and no new fault.

- [ ] **Step 8: Commit**

```bash
git add ov13850_opi5pro_learning/mpp
git commit -m "refactor(mpp): add encoded packet sink"
```

### Task 4: Extract Reusable V4L2 Capture

**Files:**
- Create: `ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp`
- Create: `ov13850_opi5pro_learning/mpp/tests/check_v4l2_capture_compile.cpp`
- Modify: `ov13850_opi5pro_learning/mpp/src/v4l2_mpp_encoder.cpp`
- Modify: `ov13850_opi5pro_learning/mpp/Makefile`

- [ ] **Step 1: Write a failing ARM64 compile contract**

The compile-only test must construct:

```cpp
V4L2Capture capture("/dev/video11", V4L2MemoryMode::DmaBufExport);
```

and type-check `start()`, `dequeue()`, `requeue()`, `mpp_buffer(index)`, and `stop()`.

- [ ] **Step 2: Run compile and verify missing header failure**

Use `aarch64-linux-gnu-g++ -fsyntax-only` with official MPP include path.

- [ ] **Step 3: Move capture ownership into the shared header**

Preserve these invariants:

```text
REQBUFS count = 4
MMAP remains available for copy validation
EXPBUF/import occurs once before STREAMON
DQBUF does not automatically QBUF
DMA-BUF is QBUF only after MPP consume returns
cleanup order: MppBuffer put -> export fd close -> munmap -> video fd close
```

- [ ] **Step 4: Reduce the Stage 4 live encoder to orchestration**

Remove duplicate V4L2 class code and include `v4l2_capture.hpp`. Do not change output or command-line behavior.

- [ ] **Step 5: Compile and run Stage 4 live regressions**

Require the deterministic color-bar copy and DMA-BUF outputs to remain byte-identical.

- [ ] **Step 6: Commit**

```bash
git add ov13850_opi5pro_learning/mpp
git commit -m "refactor(mpp): share v4l2 capture ownership"
```

### Task 5: Package MPP Development Inputs and Native Streaming Build

**Files:**
- Modify: `ov13850_opi5pro_learning/mpp/scripts/package_mpp_bundle.sh`
- Modify: `ov13850_opi5pro_learning/mpp/tests/test_fetch_build_mpp.sh`
- Create: `ov13850_opi5pro_learning/streaming/Makefile`
- Create: `ov13850_opi5pro_learning/streaming/scripts/package_streaming_source.sh`
- Modify: `ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh`

- [ ] **Step 1: Add failing bundle assertions**

Require at least these official headers in the bundle:

```text
include/rockchip/rk_mpi.h
include/rockchip/mpp_buffer.h
include/rockchip/mpp_frame.h
include/rockchip/mpp_packet.h
include/rockchip/rk_venc_cfg.h
```

- [ ] **Step 2: Run and verify failure**

Expected: current runtime bundle has no `include/rockchip`.

- [ ] **Step 3: Extend the official bundle**

Copy the pinned SDK headers into the bundle and include them in `SHA256SUMS`. Do not copy MPP source or install files under `/usr`.

- [ ] **Step 4: Implement native streaming Makefile**

Use:

```make
CXX ?= g++
MPP_BUNDLE ?= ../mpp/build/bundle/official-mpp
GST_MODULES := gstreamer-1.0 gstreamer-app-1.0
GST_RTSP_MODULES := $(GST_MODULES) gstreamer-rtsp-server-1.0
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
```

Link with MPP bundle rpath and `pkg-config` GStreamer flags. Separate `rtp` and `rtsp` targets so RTP can build before RTSP development headers are installed.

- [ ] **Step 5: Implement source package script**

Create one tarball containing streaming sources/scripts/tests, shared MPP headers, and the official MPP bundle. Generate a SHA-256 beside it.

- [ ] **Step 6: Run package tests and board-native smoke build**

Codex transfers the tarball. On the board, build `rtp` with native `g++` and verify `ldd` resolves the bundled `librockchip_mpp.so.1` and system GStreamer.

- [ ] **Step 7: Commit**

```bash
git add ov13850_opi5pro_learning/mpp ov13850_opi5pro_learning/streaming
git commit -m "build(streaming): add native gstreamer bundle"
```

### Task 6: Implement and Test GstRtpSink

**Files:**
- Create: `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.hpp`
- Create: `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.cpp`
- Create: `ov13850_opi5pro_learning/streaming/tests/test_gst_rtp_sink.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/Makefile`

- [ ] **Step 1: Write the failing board-native fakesink test**

The test pushes one codec-config packet and three frames with `pts_us` values 0, 33333, and 66666. A test pipeline ending in appsink/fakesink must observe three timed access units with monotonic PTS and approximately 33.333 ms duration.

- [ ] **Step 2: Build and verify failure**

Expected: missing `GstRtpSink` implementation.

- [ ] **Step 3: Implement the public sink contract**

```cpp
struct RtpSinkConfig {
    std::string host;
    int port = 5004;
    int payload_type = 96;
    int mtu = 1200;
    int queue_buffers = 2;
};

class GstRtpSink final : public EncodedPacketSink {
public:
    explicit GstRtpSink(const RtpSinkConfig &config);
    void consume(const EncodedPacketView &packet) override;
    void end_of_stream();
    void throw_on_bus_error();
    std::uint64_t queue_overruns() const;
};
```

- [ ] **Step 4: Implement GstBuffer timestamping**

For normal frames:

```cpp
GST_BUFFER_PTS(buffer) = packet.pts_us * GST_USECOND;
GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 30);
```

For codec config, set `GST_BUFFER_FLAG_HEADER` and no timestamp. Copy bytes into GstBuffer before returning from `consume()`.

- [ ] **Step 5: Implement the RTP pipeline**

Use named `appsrc` and queue, set appsrc caps to H.264 byte-stream at 1920x1080/30, connect queue `overrun`, and monitor the bus. Set appsrc `is-live=true`, `format=GST_FORMAT_TIME`, `block=false`.

- [ ] **Step 6: Run fakesink and local UDP tests**

Require monotonic timestamps, successful EOS, no bus error, and bounded queue behavior.

- [ ] **Step 7: Commit**

```bash
git add ov13850_opi5pro_learning/streaming
git commit -m "feat(streaming): add gstreamer rtp packet sink"
```

### Task 7: Implement Live V4L2/MPP RTP Sender

**Files:**
- Create: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`
- Create: `ov13850_opi5pro_learning/streaming/tests/test_rtp_sender_board.sh`
- Modify: `ov13850_opi5pro_learning/streaming/Makefile`
- Modify: `ov13850_opi5pro_learning/streaming/README.md`

- [ ] **Step 1: Write the failing board acceptance wrapper**

The wrapper runs 300 frames and requires output keys:

```text
mode=dmabuf
frames_in=300 frames_sent=300
timeouts=0 dropped=0
rtp_clock_rate=90000
queue_overruns=0
STREAM_RTP_OK
```

It also checks PM suspended/usage 0 and scans new kernel logs for CIF/ISP/MPP/RKVENC/MMU/IOMMU faults.

- [ ] **Step 2: Build and verify missing sender failure**

Expected: missing `v4l2_mpp_rtp_sender` executable.

- [ ] **Step 3: Implement CLI and defaults**

```text
--device /dev/video11
--host <Windows IPv4>
--port 5004
--frames 300
--bitrate 8000000
--gop 30
--mtu 1200
--queue-buffers 2
--mode dmabuf|copy
```

Reject invalid addresses, ports, nonpositive values, and unsupported mode before opening the camera.

- [ ] **Step 4: Implement the live loop**

Use shared `V4L2Capture`, skip the first three frames, track sequence gaps, submit imported DMA-BUF to MPP, synchronously push the returned packet to `GstRtpSink`, then QBUF. Check GStreamer bus errors every frame.

Install SIGINT/SIGTERM handling that stops after the current frame and performs normal cleanup.

- [ ] **Step 5: Run board loopback and Windows receive**

First send to a board-local UDP receiver/fakesink. Then user starts the Windows receiver while Codex runs the live sender.

Require visible moving video, 30 fps sender loop, 0 timeout/drop, and no unbounded queue overrun.

- [ ] **Step 6: Decode and packet-timing validation**

Capture RTP packets on Windows. Verify payload 96, encoding H264, clock-rate 90000, increasing sequence numbers, marker on access-unit end, and approximately 3000 timestamp units per 30 fps frame.

- [ ] **Step 7: Commit**

```bash
git add ov13850_opi5pro_learning/streaming docs/codex/stage5_rtp_streaming_validation.md
git commit -m "feat(streaming): send live mpp h264 over rtp"
```

### Task 8: Tune Low-Latency RTP and Measure End-to-End Delay

**Files:**
- Modify: `ov13850_opi5pro_learning/streaming/scripts/receive_h264_rtp.ps1`
- Modify: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtp_sender.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/tests/test_rtp_sender_board.sh`
- Modify: `docs/codex/stage5_rtp_streaming_validation.md`

- [ ] **Step 1: Record untuned baseline**

Keep sender queue at 2 and receiver jitter latency at 100 ms. Record FPS, process CPU/RSS, queue overruns, sender bitrate, and user-observed phone measurement.

- [ ] **Step 2: Test one receiver parameter at a time**

Compare jitter buffer latency 100, 50, 30, and 10 ms. Keep sender, scene, decoder, sink, and network unchanged. Reject settings that cause persistent artifacts or stalls.

- [ ] **Step 3: Test sender GOP and queue bounds**

Compare GOP 60 and 30, then queue size 2 and 1. Verify new receiver recovery time and queue overruns. Keep B frames disabled.

- [ ] **Step 4: Add congestion response**

When queue overrun count increases, log it and request an IDR once per bounded cooldown instead of requesting one on every dropped frame.

- [ ] **Step 5: Perform phone latency measurement**

User cooperation: display a millisecond timer, point OV13850 at it, and use a phone to capture both the source timer and Windows playback. Record at least five samples, including minimum, maximum, and representative value.

Acceptance: every representative run is no greater than 200 ms and delay does not grow over time.

- [ ] **Step 6: Commit evidence**

```bash
git add ov13850_opi5pro_learning/streaming docs/codex/stage5_rtp_streaming_validation.md
git commit -m "perf(streaming): tune rtp latency and recovery"
```

### Task 9: Add Shared RTSP Server and Reconnect Recovery

**Files:**
- Create: `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.hpp`
- Create: `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp`
- Create: `ov13850_opi5pro_learning/streaming/src/v4l2_mpp_rtsp_server.cpp`
- Create: `ov13850_opi5pro_learning/streaming/tests/test_rtsp_recovery.sh`
- Modify: `ov13850_opi5pro_learning/streaming/Makefile`
- Create: `docs/codex/stage5_rtsp_recovery_validation.md`

- [ ] **Step 1: Write the failing RTSP recovery test**

The test starts the server, connects a GStreamer client, counts decoded frames, stops the client, reconnects, and requires decoded frames again without restarting the server. Require log markers `RTSP_CLIENT_CONNECTED`, `IDR_REQUESTED`, and `RTSP_CLIENT_DISCONNECTED`.

- [ ] **Step 2: Build and verify missing server failure**

Expected: no `v4l2_mpp_rtsp_server` target.

- [ ] **Step 3: Implement shared RTSP media factory**

Use a shared factory and pipeline:

```text
appsrc name=source is-live=true format=time block=false
-> queue max-size-buffers=2 leaky=downstream
-> h264parse
-> rtph264pay name=pay0 pt=96 mtu=1200 config-interval=1
```

On media configure, obtain and retain the appsrc with explicit ref-count ownership. On unprepare, stop using it before unref. Protect the current appsrc pointer with a mutex because capture/encode runs outside the GLib main-loop thread.

- [ ] **Step 4: Implement capture/encoder worker**

Keep one camera and one MPP encoder for the shared stream. If no client appsrc exists, consume and discard packets without blocking capture. On a new client, push codec header, request IDR, then resume timed access units.

- [ ] **Step 5: Handle errors and shutdown**

Convert GStreamer bus errors into process failure; stop worker, STREAMOFF, join thread, release RTSP objects, and verify PM suspended/0.

- [ ] **Step 6: Run reconnect tests**

Test GStreamer client restart and VLC reconnect. Require recovery at the next IDR/SPS/PPS and no server restart.

- [ ] **Step 7: Commit**

```bash
git add ov13850_opi5pro_learning/streaming docs/codex/stage5_rtsp_recovery_validation.md
git commit -m "feat(streaming): add shared rtsp server"
```

### Task 10: Stage 5 Regression, Documentation, and Closure

**Files:**
- Modify: `ov13850_opi5pro_learning/streaming/README.md`
- Modify: `docs/codex/README.md`
- Modify: `docs/codex/HANDOFF.md`
- Modify: `docs/codex/task_plan.md`
- Modify: `docs/codex/progress.md`
- Modify: `docs/codex/findings.md`
- Modify: `docs/codex/orangepi5pro-kernel-troubleshooting.md`
- Modify: `ov13850_opi5pro_learning/orangepi5pro-kernel-troubleshooting.md`
- Modify: `docs/codex/project_source_file_index.md` if that index has been implemented before Stage 5 closure.

- [ ] **Step 1: Run all static and host tests**

```bash
git diff --check
bash -n ov13850_opi5pro_learning/mpp/scripts/*.sh
bash -n ov13850_opi5pro_learning/mpp/tests/*.sh
bash -n ov13850_opi5pro_learning/streaming/scripts/*.sh
bash -n ov13850_opi5pro_learning/streaming/tests/*.sh
bash ov13850_opi5pro_learning/streaming/tests/test_streaming_scripts.sh
```

- [ ] **Step 2: Clean-build official MPP and board-native streaming programs**

Require `MPP_SDK_OK`, `MPP_BUNDLE_OK`, native RTP/RTSP binaries, correct bundled MPP resolution, and successful GStreamer plugin loading.

- [ ] **Step 3: Run Stage 4 regression matrix**

Re-run H.264/H.265 file tests, parameter matrix, live copy, and live DMA-BUF. Require previous behavior, complete decode counts, PM suspended/0, and no new fault.

- [ ] **Step 4: Run Stage 5 acceptance**

Require:

```text
1920x1080@30 Windows playback
>=300 live frames
V4L2 timeout=0
no sustained queue growth
90 kHz RTP timing
representative end-to-end latency <=200 ms
receiver restart recovery
RTSP reconnect recovery
PM suspended/usage 0 after exit
no new camera/MPP/IOMMU fault
```

- [ ] **Step 5: Update documentation**

Record exact commands, versions, packet evidence, queue/GOP/jitter settings, five latency samples, recovery times, CPU/RSS, known limitations, and user cooperation steps. Mark Stage 5 complete only when every acceptance item has evidence; otherwise keep Stage 5 current and list the exact blocker.

- [ ] **Step 6: Verify documentation links and troubleshooting copies**

Check every new relative Markdown link exists. Require byte-identical troubleshooting copies using `cmp` and matching SHA-256.

- [ ] **Step 7: Commit closure**

```bash
git add docs/codex ov13850_opi5pro_learning
git commit -m "docs: record stage 5 streaming validation"
```

- [ ] **Step 8: Finish the branch**

Use `superpowers:verification-before-completion`, then `superpowers:finishing-a-development-branch`. Do not push automatically. Present local merge/push choices to the user after evidence is complete.
