# OV13850 Learning Driver Stage 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to execute this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `ov13850_i2c_min.c` into a compact, standards-oriented V4L2
OV13850 learning driver with two RAW10 modes, controls, runtime PM, and
safe cleanup, without changing the formal driver or Device Tree.

**Architecture:** The learning driver stays isolated behind
`learning,ov13850-i2c`. `struct ov13850_min` owns V4L2 state, controls, and
power state. Format negotiation selects one of two copied, known sensor modes;
stream control takes runtime-PM references and writes mode/control registers.

**Tech Stack:** Linux 6.1 V4L2 sub-device API, media controller, I2C, runtime
PM, regulator/GPIO/clock frameworks, OV13850 register interface.

---

## Boundaries

- Modify only `drivers/media/i2c/ov13850_i2c_min.c` during implementation.
- Use `drivers/media/i2c/ov13850.c` as read-only reference.
- Do not touch Device Tree, `ov13850.c`, D-PHY/CSI/CIF/ISP drivers, or the
  unrelated `ov13855-2@36` node.
- Do not build, load a module, deploy an Image, connect to the board, or run
  any runtime validation until Tasks 1-5 are all written. Task 6 is the single
  integrated validation pass requested by the user.

### Task 1: Establish the isolated driver and V4L2 state (2A)

**Files:**
- Modify: `drivers/media/i2c/ov13850_i2c_min.c:1-100, 1366-1545`
- Reference: `drivers/media/i2c/ov13850.c:90-180, 1560-1640`

- [ ] **Step 1: Add the framework declarations used by later tasks.**

  Add `#include <linux/pm_runtime.h>` and `#include <media/v4l2-ctrls.h>`.
  Add `struct v4l2_ctrl_handler ctrl_handler;` and pointers named `exposure`,
  `anal_gain`, `hblank`, `vblank`, and `test_pattern` to `struct ov13850_min`.
  Keep the existing `lock`, `powered`, `streaming`, `cur_mode`, `sd`, `pad`,
  and `fmt` fields.

- [ ] **Step 2: Keep the only legal binding alias.**

  Change the I2C ID table to contain only the learning name:

  ```c
  static const struct i2c_device_id ov13850_min_id[] = {
      { "ov13850_i2c_min", 0 },
      { }
  };
  ```

  Retain the OF entry `{ .compatible = "learning,ov13850-i2c" }`. Do not keep
  `"ovti,ov13850"` anywhere in this learning driver.

- [ ] **Step 3: Make clientdata retrieval explicit and safe.**

  Use this pattern in all client-based callbacks:

  ```c
  struct v4l2_subdev *sd = i2c_get_clientdata(client);
  struct ov13850_min *cam = to_ov13850_min(sd);
  ```

  The existing `ov13850_min_from_client()` helper can retain that conversion.
  Never cast `i2c_get_clientdata(client)` directly to `struct ov13850_min *`.

- [ ] **Step 4: Restructure probe/remove ownership.**

  In probe, initialize controls before registering the sub-device. After a
  successful registration, call:

  ```c
  pm_runtime_set_active(dev);
  pm_runtime_enable(dev);
  pm_runtime_idle(dev);
  ```

  In every probe failure path after control initialization, call
  `v4l2_ctrl_handler_free(&cam->ctrl_handler)`. In remove, unregister the
  sub-device, clean the media entity, free controls, disable runtime PM, and
  power off only if PM status is not suspended.

### Task 2: Add the V4L2 control model (2C)

**Files:**
- Modify: `drivers/media/i2c/ov13850_i2c_min.c:25-100, after s_stream()`
- Reference: `drivers/media/i2c/ov13850.c:1280-1425`

- [ ] **Step 1: Add register and range constants.**

  Define `OV13850_VTS_MAX` as `0x7fff`, exposure register `0x3500`, gain
  registers `0x350a` and `0x350b`, test-pattern register `0x5e00`, exposure
  minimum `2`, gain range `0x10..0xf8`, and the four named color-bar patterns.
  Reuse the existing `OV13850_LINK_FREQ_300MHZ`, pixel-rate macro, and mode
  HTS/VTS values.

- [ ] **Step 2: Implement `ov13850_min_set_ctrl()`.**

  The function obtains `cam` from `ctrl->handler`. Before a runtime-PM get,
  handle `V4L2_CID_VBLANK` by changing exposure maximum to:

  ```c
  cam->cur_mode->height + ctrl->val - 16
  ```

  If `pm_runtime_get_if_in_use(&client->dev)` returns zero, return success
  without I2C. Otherwise map controls as follows:

  ```text
  EXPOSURE       0x3500..0x3502, 24-bit write of ctrl->val << 4
  ANALOGUE_GAIN  0x350a high 3 bits, then 0x350b low 8 bits
  VBLANK         0x380e..0x380f, 16-bit write of height + ctrl->val
  TEST_PATTERN   0x5e00, 0 disables; otherwise 0x80 | (pattern - 1)
  ```

  Always `pm_runtime_put()` after a successful PM get and return the I2C error
  rather than masking it.

- [ ] **Step 3: Implement `ov13850_min_init_controls()`.**

  Initialize the handler with seven controls and set `handler->lock =
  &cam->lock`. Create LINK_FREQ, PIXEL_RATE, and HBLANK as read-only controls.
  Create writable VBLANK, EXPOSURE, ANALOGUE_GAIN, and TEST_PATTERN controls
  using `.s_ctrl = ov13850_min_set_ctrl`. Attach the handler to
  `cam->sd.ctrl_handler`; free it and return `handler->error` on construction
  failure.

### Task 3: Move power ownership to runtime PM and complete stream control (2B, 2D)

**Files:**
- Modify: `drivers/media/i2c/ov13850_i2c_min.c:796-940`
- Reference: `drivers/media/i2c/ov13850.c:990-1065, 1190-1208`

- [ ] **Step 1: Split stream work from power work.**

  `ov13850_min_start_streaming()` must write global init, write `cur_mode`,
  call `v4l2_ctrl_handler_setup(&cam->ctrl_handler)`, then write `0x0100 =
  0x01`. It must not call `ov13850_min_power_on()`.

  `ov13850_min_stop_streaming()` writes `0x0100 = 0x00` only.

- [ ] **Step 2: Add runtime PM callbacks and device PM ops.**

  Implement:

  ```c
  static int ov13850_min_runtime_resume(struct device *dev)
  {
      struct ov13850_min *cam = ov13850_min_from_client(to_i2c_client(dev));
      return ov13850_min_power_on(cam);
  }

  static int ov13850_min_runtime_suspend(struct device *dev)
  {
      struct ov13850_min *cam = ov13850_min_from_client(to_i2c_client(dev));
      ov13850_min_power_off(cam);
      return 0;
  }
  ```

  Place them in `static const struct dev_pm_ops ov13850_min_pm_ops` through
  `SET_RUNTIME_PM_OPS()`, then attach `.pm = &ov13850_min_pm_ops` to the I2C
  driver's `.driver` block.

- [ ] **Step 3: Make `s_stream()` own PM references.**

  On stream-on, call `pm_runtime_get_sync()`. If it returns a negative value,
  call `pm_runtime_put_noidle()` and return that error. If later stream setup
  fails, call `pm_runtime_put()` before returning. On stream-off, write standby
  first, then call `pm_runtime_put()`. Set `cam->streaming` only after the
  corresponding path succeeds.

### Task 4: Complete two-mode format negotiation (2E)

**Files:**
- Modify: `drivers/media/i2c/ov13850_i2c_min.c:512-600, 1220-1365`
- Reference: `drivers/media/i2c/ov13850.c:646-682, 760-885, 1210-1258`

- [ ] **Step 1: Add the full-resolution register table and mode.**

  Copy only `ov13850_4224x3136_regs[]` from the formal driver, adapting its
  `struct regval` entries to `struct ov13850_regval`, and terminate it with
  `OV13850_REG_END`. Add a mode with width `4224`, height `3136`, frame
  interval `20000/150000`, `hts_def = 0x12c0`, `vts_def = 0x0d00`, and its
  copied register table.

- [ ] **Step 2: Replace single-mode enumeration with a mode array.**

  Store both modes in a `supported_modes[]` array. Make frame-size and
  frame-interval enumeration reject an index outside this array or a code
  other than `MEDIA_BUS_FMT_SBGGR10_1X10`.

- [ ] **Step 3: Implement TRY and ACTIVE format semantics.**

  Select the closest mode by absolute width plus height distance. Fill format
  code, width, height, and `V4L2_FIELD_NONE`. TRY changes only
  `v4l2_subdev_get_try_format()`. ACTIVE returns `-EBUSY` when streaming;
  otherwise it sets `cur_mode` and updates HBLANK, VBLANK, and exposure ranges
  with `__v4l2_ctrl_modify_range()`.

- [ ] **Step 4: Report link configuration and default TRY format.**

  Keep two CSI-2 lanes and continuous clock in `get_mbus_config()`. Add the
  sub-device `.open` internal operation that initializes its TRY format to the
  2112x1568 default mode.

### Task 5: Finish lifecycle cleanup and source review (2F code portion)

**Files:**
- Modify: `drivers/media/i2c/ov13850_i2c_min.c:1366-1545`
- Reference: `drivers/media/i2c/ov13850.c:1560-1640`

- [ ] **Step 1: Audit error labels in probe.**

  Order cleanup labels so every acquired resource is released exactly once:
  unregister subdev, clean media entity, free controls, remove sysfs group,
  disable PM if enabled, power off if active, and destroy mutex.

- [ ] **Step 2: Audit remove against the same ownership order.**

  Remove sysfs first, unregister the V4L2 sub-device, clean the entity, free
  controls, disable PM, power off an active device, set the PM state suspended,
  then destroy the mutex.

- [ ] **Step 3: Perform the deferred source-only review.**

  Check that no `ovti,ov13850` learning alias remains, no direct power call
  occurs inside `s_stream()`, all I2C control writes return errors, and neither
  `streaming` nor `powered` can be marked true on a failed operation. Do not
  run a compiler or board command at this point.

### Task 6: One integrated Stage 2 validation pass (deferred by user request)

**Files:**
- Inspect: `drivers/media/i2c/ov13850_i2c_min.c`
- Build output: `out/orangepi5pro-2a-learning/`

- [ ] **Step 1: Establish a load-safe build output.**

  Build `Image` and then `ov13850_i2c_min.ko` in the same `O=` tree. Confirm
  matching `vmlinux`, `Module.symvers`, kernel release, and module vermagic.

- [ ] **Step 2: Verify alias isolation before module installation.**

  Confirm `modinfo` includes the learning OF alias but contains neither
  `i2c:ovti,ov13850` nor any binding that could replace the production sensor.

- [ ] **Step 3: Perform controlled board checks.**

  First load/unload without changing the active CAM2 binding. Only then use a
  reversible test DT configuration to bind the learning driver and run both
  mode/format/control checks, `v4l2-compliance`, repeated stream start/stop,
  sustained capture, and D-PHY/CIF/ISP log inspection.
