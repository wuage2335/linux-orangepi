# OV13850 RKAIQ/3A Learning Bundle

This directory builds a private RK3588 ISP3.0 RKAIQ runtime. It never installs
or replaces system libraries.

```bash
./scripts/fetch_build_rkaiq.sh
```

The generated `build/bundle` contains the server, private library, module-info
probe/shim, IQ compatibility converter, NOTICE, origin metadata and
SHA256SUMS. The pinned upstream source is not committed to this kernel
repository.

Run the private bundle without installing system files:

```bash
RKAIQ_BUNDLE=/path/to/bundle ./scripts/run_rkaiq_local.sh
```

The launcher generates a compatible IQ copy under the bundle runtime
directory, enables the module-info shim, and selects single-camera online
mode. It never modifies `/etc/iqfiles`.

Current board status: initialization, online capture, dynamic ISP statistics,
AE and per-frame AWB processing pass as an unprivileged user. The compatibility
patch falls back from `SCHED_RR` to `SCHED_OTHER` when `CAP_SYS_NICE` is absent.
Bright/normal/dark subjective image-quality acceptance remains manual. See
`docs/codex/stage6_rkaiq_3a_validation.md`.
