import json
from pathlib import Path
import re
import statistics

root = Path(__file__).parent / 'results'
rows = []
for path in sorted(root.glob('cpu*.log')):
    samples = [json.loads(line) for line in path.read_text().splitlines()]
    assert all(row['valid'] for row in samples)
    values = [row['payload_MB_s'] for row in samples]
    rows.append({'case': path.stem, 'MB_s_median': statistics.median(values),
                 'MB_s_min': min(values), 'MB_s_max': max(values), 'measurements': len(values)})
for op in ('read','write','copy'):
    aggregate = []
    for trial in range(1,4):
        samples = [json.loads(path.read_text().strip()) for path in sorted(root.glob(f'big4-{op}-trial{trial}-*.log'))]
        assert len(samples) == 4 and all(row['valid'] for row in samples)
        window = max(row['end_s'] for row in samples) - min(row['start_s'] for row in samples)
        aggregate.append(sum(row['bytes'] * row['iterations'] for row in samples) / window / 1e6)
    rows.append({'case': 'big4-' + op, 'MB_s_median': round(statistics.median(aggregate),3),
                 'MB_s_min': round(min(aggregate),3), 'MB_s_max': round(max(aggregate),3), 'measurements': 3})
telemetry = [json.loads(line) for line in (root/'telemetry.jsonl').read_text().splitlines()]
temperature = [int(row['soc_millic']) / 1000 for row in telemetry if row['soc_millic']]
stages = {}
for stage in sorted({row['stage'] for row in telemetry}):
    selected = [row for row in telemetry if row['stage'] == stage]
    loads = [int(m[1]) for row in selected if (m := re.match(r'(\d+)@', row['dmc_load'] or ''))]
    stages[stage] = {'dmc_hz': sorted({int(row['dmc_hz']) for row in selected if row['dmc_hz']}),
                     'load_max_percent': max(loads) if loads else None}
stamp = re.compile(r'^\[\s*([\d.]+)\]')
before = (root/'dmesg-before.txt').read_text()
cutoff = max(float(m[1]) for line in before.splitlines() if (m := stamp.match(line)))
new_lines = [line for line in (root/'dmesg-after.txt').read_text().splitlines()
             if (m := stamp.match(line)) and float(m[1]) > cutoff]
report = {'results': rows, 'soc_temperature_max_c': max(temperature),
          'stages': stages, 'new_kernel_messages': new_lines,
          'background_caveat': 'CPU0 TASKLET softirq storm was present before/during tests',
          'units': 'decimal MB/s of CPU payload; copy counts N, not 2N'}
(root/'analysis.json').write_text(json.dumps(report,indent=2) + '\n')
print(json.dumps({'results': rows, 'temperature_max_c': max(temperature),
                  'new_kernel_messages': new_lines}, indent=2))
