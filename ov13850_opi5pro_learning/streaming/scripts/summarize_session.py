#!/usr/bin/env python3
"""读取 session 原始记录，输出温度和单核百分比口径的进程 CPU/RSS 摘要。"""
import argparse
import json
import os
from pathlib import Path
import re


def summarize(root, ticks=100):
    root = Path(root)
    rows = [json.loads(line) for line in (root / 'telemetry.jsonl').read_text().splitlines()]
    result = {'samples': len(rows), 'clock_ticks_per_second': ticks, 'processes': {}, 'temperature_c': {}}
    temperatures, processes = {}, {}
    for row in rows:
        for zone, value in row['temperature_c'].items():
            temperatures.setdefault(zone, []).append(value)
        for name, data in row['processes'].items():
            # /proc/pid/stat 的 comm 字段可以含空格，先跳过最后一个右括号。
            fields = data['stat'].rsplit(')', 1)[1].split()
            ticks_used = int(fields[11]) + int(fields[12])
            rss = re.search(r'^VmRSS:\s+(\d+) kB', data['status'], re.M)
            processes.setdefault(name, []).append((row['monotonic_s'], ticks_used, int(rss[1]) if rss else None))
    for zone, values in temperatures.items():
        result['temperature_c'][zone] = {'start': values[0], 'end': values[-1], 'min': min(values), 'max': max(values)}
    for name, samples in processes.items():
        first, last = samples[0], samples[-1]
        duration = last[0] - first[0]
        rss = [row[2] for row in samples if row[2] is not None]
        result['processes'][name] = {
            'observed_s': round(duration, 3),
            'cpu_percent_one_core': round((last[1] - first[1]) / ticks / duration * 100, 3) if duration else None,
            'rss_kib_min': min(rss) if rss else None, 'rss_kib_max': max(rss) if rss else None}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    parser.add_argument('--clock-ticks', type=int, default=100, help='取采样板端 getconf CLK_TCK，默认100')
    args = parser.parse_args()
    print(json.dumps(summarize(args.directory, args.clock_ticks), indent=2, ensure_ascii=False))
