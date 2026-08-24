# Official MPP Environment Validation

日期：2026-08-25

## 环境

- Board: Orange Pi 5 Pro, RK3588S
- Kernel: `6.1.99-opi5pro-livecfg-baseline`
- Device: `/dev/mpp_service`
- Kernel support: RKVENC HW ID `0x50603312`, VEPU2
- User library: official Rockchip MPP 1.1.0
- Commit: `c08762ebfadeb4e986d2fed993bc7a54862d3ebe`
- Deploy archive SHA-256:
  `ab0fdef57ee7f4fb0745b0d46fbd8d09758ef9d06c0a04c94ca6d7e6d5e8a12c`

## MPP compatibility

`mpp_info_test` successfully loaded the bundled library and reported commit
`c08762ebf`. It also reported client 4 and client 12 unavailable; the RKVENC path used
by H.264 encoding remained available, so these messages are not blockers for this stage.

## Official generated-frame H.264

Command contract:

```text
1920x1080 NV12, H.264 type 7, CBR 8 Mbps, 30 fps, GOP 60, 30 frames
```

Result:

```text
encoded frames=30
elapsed=216 ms
encoder fps=138.35
output size=337 KiB
SHA-256=08c30c5aa02faa88b2b9472803f3575d6ae528499335a7d059d3f749e3c0e810
test_ret=0
```

The Annex-B stream starts with H.264 SPS (`0x67`), PPS (`0x68`), and IDR (`0x65`)
NAL units.

## Actual RKISP NV12 frame

The mainpath was configured to 1920x1080 NV12 and captured one 3,110,400-byte frame.
The official encoder accepted this input and produced one H.264 frame:

```text
encoded frames=1
output size=2.9 KiB
SHA-256=9fe26fa9a049255f488712655ef45eaf574f5ea99c1fdc67098369b6813be47a
test_ret=0
```

## Kernel warning boundary

At boot both RKVENC cores reported missing VENC regulator/devfreq OPP configuration, then
attached to the CCU and completed probe. Actual H.264 hardware encoding works, so the
warning is not a functional blocker. It may affect dynamic frequency control and must be
revisited only if sustained encoding shows performance or thermal limits.

No new MPP/RKVENC/IOMMU error was observed during these short official tests.

## Conclusion

Official MPP 1.1.0 is compatible with the current kernel MPP service and RKVENC. Stage 4
can proceed to the project-owned H.264 file encoder without changing the kernel or DTS.
