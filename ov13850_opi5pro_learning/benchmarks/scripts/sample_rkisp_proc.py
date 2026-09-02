#!/usr/bin/env python3

import argparse
import csv
import re
import time
from pathlib import Path


ONLINE = re.compile(
    r"Isp online frame:(\d+) state:(\w+) time:(\d+)ms v-blank:(\d+)us"
)
OUTPUT = re.compile(
    r"Output\s+rkisp_mainpath.*\(frame:(\d+) rate:(\d+)ms delay:(\d+)ms "
    r"frameloss:(\d+) bufcnt:(\d+)\)"
)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--proc", default="/proc/rkisp1-vir0")
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--interval-ms", type=float, default=8.0)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    if args.frames < 1 or args.interval_ms <= 0:
        raise SystemExit("frames and interval must be positive")
    rows = {}
    path = Path(args.proc)
    while len(rows) < args.frames:
        text = path.read_text(encoding="utf-8")
        online = ONLINE.search(text)
        output = OUTPUT.search(text)
        if online and output and online.group(1) == output.group(1):
            frame = int(output.group(1))
            rows[frame] = {
                "sample_monotonic_ns": time.monotonic_ns(),
                "frame": frame,
                "isp_state": online.group(2),
                "isp_interval_ms": int(online.group(3)),
                "vblank_us": int(online.group(4)),
                "output_rate_ms": int(output.group(2)),
                "output_delay_ms": int(output.group(3)),
                "frameloss": int(output.group(4)),
                "bufcnt": int(output.group(5)),
            }
        time.sleep(args.interval_ms / 1000.0)

    fields = list(next(iter(rows.values())).keys())
    with open(args.output, "w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for frame in sorted(rows):
            writer.writerow(rows[frame])
    print(f"RKISP_PROC_SAMPLES_OK frames={len(rows)} output={args.output}")


if __name__ == "__main__":
    main()
