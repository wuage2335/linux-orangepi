# MPP to GStreamer Zero-Copy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the per-packet CPU copy between Rockchip MPP output and GStreamer while preserving valid packet storage until the last downstream `GstBuffer` reference is released.

**Architecture:** `EncodedPacketView` gains a shared storage owner. MPP obtains a distinct output `MppBuffer` for each encoded packet and attaches a reference-counted lease to the view; GStreamer wraps that memory read-only and releases the lease from its destroy callback. RTSP stores the codec-header view and owner instead of copying the header into a vector.

**Tech Stack:** C++17, Rockchip MPP, GStreamer 1.20, GNU Make, existing host and board build scripts.

---

### Task 1: Make the GStreamer buffer conversion testable on the WSL host

**Files:**
- Modify: `ov13850_opi5pro_learning/streaming/Makefile`
- Test: `ov13850_opi5pro_learning/streaming/tests/test_gst_rtp_sink.cpp`

- [x] **Step 1: Remove the host test's unnecessary MPP runtime link**

Add a GStreamer-only environment check and build `test_gst_rtp_sink` with only GStreamer libraries:

```make
.PHONY: check-gst-env

check-gst-env:
	@command -v $(CXX) >/dev/null
	@command -v pkg-config >/dev/null
	@pkg-config --exists $(GST_MODULES)

$(RTP_SINK_TEST_BIN): ... | check-gst-env
	$(CXX) ... $$(pkg-config --libs $(GST_MODULES)) -pthread -o $@
```

- [x] **Step 2: Run the unchanged host test as a clean baseline**

Run: `make test-rtp-sink`

Expected: `PASS: GstRtpSink buffer conversion`.

### Task 2: Specify and implement ownership-preserving GstBuffer wrapping

**Files:**
- Modify: `ov13850_opi5pro_learning/mpp/src/encoded_packet_sink.hpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.cpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/gst_rtp_sink.hpp`
- Test: `ov13850_opi5pro_learning/streaming/tests/test_gst_rtp_sink.cpp`

- [x] **Step 1: Write the failing zero-copy lifetime test**

Create a packet with shared storage, convert it, then prove the returned memory points at the original bytes and keeps the owner alive:

```cpp
void test_owned_packet_wraps_without_copy()
{
	auto storage = std::make_shared<std::array<std::uint8_t, 5>>(
		std::array<std::uint8_t, 5>{0x00, 0x00, 0x01, 0x65, 0xaa});
	std::weak_ptr<const void> lifetime = storage;
	const EncodedPacketView packet = {
		storage->data(), storage->size(), 33333, true, false, false, storage,
	};
	GstBuffer *buffer = make_gst_buffer(packet);
	storage.reset();
	require(!lifetime.expired(), "GstBuffer released packet storage early");
	GstMapInfo map = GST_MAP_INFO_INIT;
	require(gst_buffer_map(buffer, &map, GST_MAP_READ), "buffer map failed");
	require(map.data == packet.data, "owned packet payload was copied");
	gst_buffer_unmap(buffer, &map);
	gst_buffer_unref(buffer);
	require(lifetime.expired(), "GstBuffer retained packet storage after release");
}
```

- [x] **Step 2: Run the test and verify RED**

Run: `make test-rtp-sink`

Expected: compilation fails because `EncodedPacketView` has no storage owner, or the pointer-identity assertion fails while `make_gst_buffer` still copies.

- [x] **Step 3: Add the packet storage owner**

Extend the view without changing synchronous sinks:

```cpp
std::shared_ptr<const void> owner = {};
```

- [x] **Step 4: Wrap owned storage and retain the copy fallback**

Use `gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY, ...)` when `owner` is present. Put a heap-held `shared_ptr` in `user_data` and delete it in the destroy callback. Keep the allocation-and-fill path for stack-backed packets used by synchronous callers and existing tests.

- [x] **Step 5: Run the test and verify GREEN**

Run: `make test-rtp-sink`

Expected: all buffer metadata, pipeline lifecycle, pointer identity, and release-timing checks pass.

### Task 3: Transfer MPP output-buffer lifetime to GStreamer

**Files:**
- Modify: `ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp`
- Modify: `ov13850_opi5pro_learning/streaming/src/gst_rtsp_server.cpp`

- [x] **Step 1: Allocate an output buffer per in-flight packet**

Replace the single reusable `packet_buffer_` with a local `MppBuffer` obtained from the existing internal group before creating each `MppPacket`:

```cpp
MppBuffer output_buffer = nullptr;
check_mpp(mpp_buffer_get(group_, &output_buffer, kFrameSize),
	  "mpp_buffer_get(packet)");
```

Every error path must deinitialize the packet and `mpp_buffer_put(output_buffer)` exactly once.

- [x] **Step 2: Attach a lease for the actual returned packet buffer**

Before calling the sink, increment the returned `mpp_packet_get_buffer(packet)` reference and create an aliasing shared owner whose deleter calls `mpp_buffer_put`. Store that owner in `EncodedPacketView`. After synchronous delivery, deinitialize the packet and release the function's local output-buffer reference; retained GStreamer buffers keep their extra reference.

- [x] **Step 3: Preserve RTSP packet ownership and eliminate header copying**

Store the codec-header `EncodedPacketView` in `GstRtspServerSink`, including its owner. When adjusting live PTS, propagate `packet.owner` into the new view. Push cached header and live packet outside the state mutex as before.

- [x] **Step 4: Cross-build streaming binaries**

Run: `./scripts/build_board.sh`

Expected: aarch64 RTP and RTSP binaries build with `-Werror` and the MPP/GStreamer SDKs.

### Task 4: Verify behavior, document boundaries, and preserve rollback

**Files:**
- Modify: `docs/codex/camera_data_flow_and_copy_analysis.md`
- Modify: `docs/codex/progress.md`

- [x] **Step 1: Run all host tests**

Run: `make test-rtp-sink test-congestion test-live-pts`

Expected: all three tests report PASS.

- [x] **Step 2: Run static quality checks**

Run: `git diff --check`

Expected: no whitespace errors.

- [x] **Step 3: Run board regression when the board is reachable**

Deploy only the streaming bundle, run the existing DMA-BUF RTSP regression with Windows decode, then verify frame count, timeout/drop counters, decode success, reconnect, sensor PM state, and kernel fault scan. Retain the old deployment for rollback.

- [x] **Step 4: Record the precise claim**

Document that V4L2-to-MPP raw frames and MPP-to-GStreamer compressed payloads avoid CPU payload copies. Do not call RTP packetization, socket transfer, Wi-Fi, or Windows display end-to-end zero-copy without separate evidence.
