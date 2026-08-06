# OV13850 Learning Driver Stage 2 Design

**Status:** Design sections approved; document pending user review before implementation.

**Goal:** Complete the learning implementation in
`drivers/media/i2c/ov13850_i2c_min.c` through the Stage 2 V4L2 sensor-driver
topics, then perform a single integrated board-validation pass.

## Scope and boundaries

- The learning driver is the only Stage 2 implementation target. The formal
  `drivers/media/i2c/ov13850.c` is read-only reference code in this phase.
- The learning driver binds only `learning,ov13850-i2c`. It must not retain an
  `i2c:ovti,ov13850` alias and must not compete with the production driver.
- This phase does not modify Device Tree, the Rockchip D-PHY/CSI/CIF/ISP
  drivers, or the known unrelated `ov13855-2@36` DT node.
- Code is written before any build, module load, deployment, or board test.
  All such checks are deferred to one integrated Stage 2 validation pass.

## Supported sensor modes

The driver uses RAW10 BGGR, two CSI-2 lanes, and a 300 MHz link frequency.
The initial active mode is 2112x1568 at 30 fps. Stage 2E adds the second,
existing formal-driver mode rather than inventing a register sequence.

| Mode | Frame interval | HTS | VTS | Role |
| --- | --- | --- | --- | --- |
| 2112x1568 | 30 fps | `0x12c0` | `0x0680` | Default low-latency mode |
| 4224x3136 | 7.5 fps | `0x12c0` | `0x0d00` | Full-resolution mode |

## Driver state and lifecycle

`struct ov13850_min` owns clock, GPIO, regulators, V4L2 sub-device, source
pad, control handler, mutex, active mode, `powered`, and `streaming` state.
The single mutex serializes format, control, stream, and debug-register access.

```text
probe
  -> acquire resources and identify the sensor
  -> initialize v4l2 subdev, one source pad, default ACTIVE format, controls
  -> register the sensor subdev and enable runtime PM

s_stream(1)
  -> pm_runtime_get_sync
  -> write global init and active mode
  -> v4l2_ctrl_handler_setup
  -> write 0x0100 = 0x01

s_stream(0)
  -> write 0x0100 = 0x00
  -> pm_runtime_put

remove
  -> unregister subdev, clean entity and controls, disable runtime PM
  -> power off only if still active
```

An error never sets `streaming = true`. A start failure releases the runtime-PM
reference and returns the actual error. Sysfs remains a register-inspection
tool and does not control ordinary streaming.

## V4L2 controls

| Control | Access | Definition |
| --- | --- | --- |
| `V4L2_CID_LINK_FREQ` | read-only | Single menu value: 300 MHz |
| `V4L2_CID_PIXEL_RATE` | read-only | `300 MHz * 2 DDR * 2 lanes / 10 bits = 120 MHz` |
| `V4L2_CID_HBLANK` | read-only | `HTS - width` for the active mode |
| `V4L2_CID_VBLANK` | read/write | VTS written as `height + vblank` to `0x380e/0x380f` |
| `V4L2_CID_EXPOSURE` | read/write | 20-bit sensor exposure at `0x3500..0x3502`; API value shifts left 4 |
| `V4L2_CID_ANALOGUE_GAIN` | read/write | High/low gain bytes at `0x350a` and `0x350b` |
| `V4L2_CID_TEST_PATTERN` | read/write | `0x5e00`; zero disables and nonzero selects a pattern |

Changing VBLANK updates the exposure maximum to
`active_height + vblank - 16`. If the device is not runtime-resumed, a control
update changes the cached V4L2 value only; `v4l2_ctrl_handler_setup()` applies
all cached values before streaming starts.

## Format and runtime-PM design

- `enum_mbus_code()` exposes only `MEDIA_BUS_FMT_SBGGR10_1X10`.
- `enum_frame_size()` and `enum_frame_interval()` enumerate exactly the two
  supported modes.
- `set_fmt(TRY)` updates only the per-file-handle TRY format.
- `set_fmt(ACTIVE)` selects the closest supported mode and recalculates HBLANK,
  VBLANK, and the exposure range. It returns `-EBUSY` while streaming.
- `get_mbus_config()` reports a two-lane, continuous-clock CSI-2 connection on
  virtual channel 0.
- `runtime_resume()` calls `ov13850_min_power_on()` and
  `runtime_suspend()` calls `ov13850_min_power_off()`. Stream control owns PM
  references; it does not call the power helpers directly.

## Deferred integrated validation

After all Stage 2 code is written, validation proceeds in this order:

1. Build `Image` and the module in the same `O=` directory so matching
   `vmlinux` and `Module.symvers` exist.
2. Check `modinfo` vermagic and aliases; only the learning compatible is
   allowed.
3. Load and unload the module without binding it over the production sensor.
4. Bind only in a controlled, reversible test configuration.
5. Run `v4l2-compliance`, enumerate both modes and all controls, and perform
   repeated stream start/stop plus sustained capture.
6. Inspect D-PHY, CIF, ISP, and kernel logs for errors before accepting the
   phase.
