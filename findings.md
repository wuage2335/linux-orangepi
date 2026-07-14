# Findings & Decisions

## Requirements
- Confirm the current OV13850 driver state against the supplied 13-point checklist.
- Build only the driver object first, then `Image`.
- Deploy the built Image and validate sysfs before capture.
- Use the STREAMON result to identify exactly one fault layer; do not alter CMA, graph, or bindings speculatively.

## Research Findings
- The worktree has a pre-existing modification to `drivers/media/i2c/ov13850_i2c_min.c`.
- Recent commits explicitly cover `max_fps`, stream control, and frame-interval enumeration; source inspection remains required because the working tree is modified.
- `struct ov13850_mode` contains `max_fps`; the active 2112x1568 mode is `10000/300000`.
- `enum_frame_interval` is in pad ops, while `g_frame_interval` and `s_stream` are in video ops; `get_mbus_config` appears only in pad ops.
- Every listed sysfs access point obtains `cam` through `ov13850_min_from_client()` and checks it before locking.
- Probe calls `v4l2_i2c_subdev_init`, initializes entity pads, registers the async sensor subdev, then exposes sysfs. Its unwind is sysfs registration -> async registration -> entity -> power in reverse order. Remove first removes sysfs and converts `clientdata` from `v4l2_subdev *`.
- `i2c_set_clientdata(client, cam)` is absent.
- `rkcif_create_dummy_buf()` zeroes `fie`, sets pad/index/which, then calls `pad.enum_frame_interval`; a successful result makes `max_size = width * height * 2`. Its get_fmt fallback leaves `fmt.pad` uninitialized, but it is not used when this sensor callback succeeds.

## Technical Decisions
| Decision | Rationale |
|---|---|
| Treat board capture as the integration regression test | This driver depends on a physical sensor, V4L2 graph, and RKCIF; the requested one-frame capture is the authoritative end-to-end test. |

## Issues Encountered
| Issue | Resolution |
|---|---|
| `orangepi` can SSH but cannot run the requested `sudo cp` non-interactively | Image upload completed; `/boot/Image` remains unchanged and reboot/testing cannot proceed until privileged board execution is available. |
