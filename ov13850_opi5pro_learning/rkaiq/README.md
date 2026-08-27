# OV13850 RKAIQ/3A Learning Bundle

This directory builds a private RK3588 ISP3.0 RKAIQ runtime. It never installs
or replaces system libraries.

```bash
./scripts/fetch_build_rkaiq.sh
```

The generated `build/bundle` contains only `rkaiq_3A_server`, `librkaiq.so`,
NOTICE, origin metadata and SHA256SUMS. The pinned upstream source is not
committed to this kernel repository.
