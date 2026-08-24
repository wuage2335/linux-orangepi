# RKISP 1080p Configuration Script Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. The user explicitly requested no subagents, so execution stays inline.

**Goal:** Build and verify an idempotent Bash script that discovers the Orange Pi 5 Pro camera nodes by sysfs name and configures the validated 2112x1568 RAW10 -> centered crop -> 1920x1080 NV12 RKISP pipeline.

**Architecture:** One production script owns discovery, fixed-format configuration, readback validation, and concise errors. A standalone Bash test creates fake sysfs nodes and a fake `v4l2-ctl`, allowing discovery and command sequencing to be tested without camera hardware. Test-only environment overrides default to the real board paths and command.

**Tech Stack:** Bash, V4L2 `v4l2-ctl`, sysfs, shell test harness.

---

### Task 1: Add a failing happy-path test

**Files:**
- Create: `ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh`
- Create later: `ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh`

- [ ] **Step 1: Create fake sysfs and fake v4l2-ctl**

The test creates five fake entries whose `name` files match the validated board names. Its fake `v4l2-ctl` appends arguments to `V4L2_CTL_LOG`; get-format and get-selection calls return the exact expected readback.

- [ ] **Step 2: Assert successful discovery and exact configuration calls**

Run the production script with:

```bash
VIDEO4LINUX_SYSFS_ROOT="$test_root/sysfs" \
V4L2_CTL_BIN="$test_root/bin/v4l2-ctl" \
V4L2_CTL_LOG="$test_root/v4l2.log" \
bash "$script"
```

Assert output contains `CONFIGURATION_OK`, and the log contains:

```text
--set-subdev-fmt pad=0,width=2112,height=1568,code=0x3007
--set-fmt-video=width=1920,height=1080,pixelformat=NV12
--set-selection=target=crop,left=0,top=190,width=2112,height=1188
```

- [ ] **Step 3: Run the test and verify RED**

Run:

```bash
bash ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh
```

Expected: FAIL because `configure_rkisp_1080p.sh` does not exist.

### Task 2: Implement node discovery and fixed configuration

**Files:**
- Create: `ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh`
- Test: `ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh`

- [ ] **Step 1: Add strict shell setup and fixed constants**

Use Bash strict mode. Default test hooks to real board values:

```bash
VIDEO4LINUX_SYSFS_ROOT="${VIDEO4LINUX_SYSFS_ROOT:-/sys/class/video4linux}"
V4L2_CTL_BIN="${V4L2_CTL_BIN:-v4l2-ctl}"
```

Keep the RAW input, crop, and NV12 output constants fixed to the approved design.

- [ ] **Step 2: Implement unique node discovery**

Implement a function that scans `*/name`, supports exact or contains matching, and returns `/dev/<basename>`. It must fail when the match count is not exactly one. Resolve:

```text
ov13850_i2c_min (contains)
rockchip-csi2-dphy0 (exact)
rockchip-mipi-csi2 (exact)
rkcif-mipi-lvds (exact)
rkisp_mainpath (exact)
```

- [ ] **Step 3: Apply configuration in the approved order**

Call `v4l2-ctl` for the four subdev pad-0 RAW10 formats, then mainpath NV12 format, then mainpath center crop. Do not start streaming or change controls.

- [ ] **Step 4: Validate readback**

Capture `--get-fmt-video` and `--get-selection=target=crop`. Require all of:

```text
1920/1080
'NV12'
Bytes per Line : 1920
Size Image : 3110400
Left 0, Top 190, Width 2112, Height 1188
```

Print discovered mappings and `CONFIGURATION_OK` only after every check succeeds.

- [ ] **Step 5: Run the happy-path test and verify GREEN**

Run the Task 1 test. Expected: PASS.

### Task 3: Add error and idempotence tests

**Files:**
- Modify: `ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh`
- Modify: `ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh`

- [ ] **Step 1: Test a missing sensor node**

Delete the fake sensor `name` file, run the script, require nonzero status and stderr containing `ERROR:` plus `Sensor`.

- [ ] **Step 2: Test duplicate mainpath nodes**

Create a second `rkisp_mainpath` entry, run the script, require nonzero status and a duplicate-match error.

- [ ] **Step 3: Test readback mismatch**

Make the fake getter report `1280/720`; require nonzero status and an error that identifies the mainpath format mismatch.

- [ ] **Step 4: Test idempotence**

Restore valid fixtures and run the script twice. Both runs must return 0 and print `CONFIGURATION_OK`.

- [ ] **Step 5: Run syntax and complete local tests**

```bash
bash -n ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh
bash -n ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh
bash ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh
```

Expected: all commands return 0.

### Task 4: Verify on Orange Pi 5 Pro

**Files:**
- Deploy temporarily: `/home/orangepi/configure_rkisp_1080p.sh`

- [ ] **Step 1: Transfer without installing system-wide**

Copy the script to the board user home and set mode 0755. Do not modify `/usr`, `/etc`, DT, or boot files.

- [ ] **Step 2: Run twice**

Both runs must return 0, show unique node mappings, and print `CONFIGURATION_OK`.

- [ ] **Step 3: Verify final format and crop**

Use `v4l2-ctl -d <discovered-mainpath> --all` and confirm 1920x1080 NV12, size 3110400, and crop 0,190/2112x1188.

- [ ] **Step 4: Verify no stream or PM side effect**

Confirm sensor runtime status remains `suspended` and usage remains 0 immediately after script execution.

- [ ] **Step 5: Run independent 1080p capture regression**

Capture one frame with the existing `v4l2-ctl` command and require exactly 3,110,400 bytes. This command is not part of the production script.

### Task 5: Commit implementation

**Files:**
- Add: `ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh`
- Add: `ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh`

- [ ] **Step 1: Review scope**

Ensure only the two script files are staged; preserve unrelated `docs/codex/task_plan.md` changes.

- [ ] **Step 2: Commit**

```bash
git add ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh \
        ov13850_opi5pro_learning/scripts/tests/test_configure_rkisp_1080p.sh
git commit -m "feat(camera): add rkisp 1080p configuration script"
```
