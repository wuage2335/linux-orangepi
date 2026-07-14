# Task Plan: OV13850 frame-interval and sysfs verification

## Goal
Validate the current minimal OV13850 driver, make only evidence-backed corrections, build and deploy the Image, then classify the first remaining capture-layer failure from board logs.

## Current Phase
Phase 1 — code and history inspection

## Phases

### Phase 1: Inspect current source and recent changes
- [x] Check the 13 user-specified driver invariants against the working tree.
- [x] Inspect the RKCIF dummy-buffer size path and recent commit diff.
- **Status:** complete

### Phase 2: Apply the smallest required correction
- [ ] Create a focused, reproducible pre-change check where feasible.
- [ ] Edit only verified defects in the OV13850 driver.
- **Status:** pending

### Phase 3: Build locally
- [x] Build `drivers/media/i2c/ov13850_i2c_min.o`.
- [x] Build `arch/arm64/boot/Image` and record its size.
- **Status:** complete

### Phase 4: Deploy and board validation
- [x] Upload Image.
- [ ] Create timestamped board backup, replace Image, reboot (blocked by board-side sudo password requirement).
- [ ] Verify sysfs attributes and kernel logs.
- [ ] Execute the one-frame `/dev/video0` STREAMON capture and collect logs.
- **Status:** in_progress

### Phase 5: Evidence-based handoff
- [ ] Report actual results in the user's requested eight sections.
- **Status:** pending

## Decisions Made
| Decision | Rationale |
|---|---|
| Preserve current DTB, overlays, CMA, and platform bindings | User supplied evidence already narrows the fault to RKCIF dummy-buffer initialization. |
| Start with driver inspection and isolated object build | Distinguishes source/compile defects before risking board deployment. |

## Errors Encountered
| Error | Attempt | Resolution |
|---|---:|---|
| Sandboxed SCP could not create a socket | 1 | Retried with approved network escalation; upload completed. |
| Board-side `sudo` requires an interactive password | 1 | Did not replace `/boot/Image`; requires user-provided privileged execution path. |
