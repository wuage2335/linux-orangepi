import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'results'
OUT.mkdir(exist_ok=False)

def read(path):
    try: return Path(path).read_text().strip()
    except OSError: return None

def sample(stage, output):
    row = {'stage': stage, 'time': time.time(), 'monotonic_s': time.monotonic(),
           'dmc_load': read('/sys/class/devfreq/dmc/load'),
           'dmc_hz': read('/sys/class/devfreq/dmc/cur_freq'),
           'soc_millic': read('/sys/class/thermal/thermal_zone0/temp'),
           'cpu_hz_khz': {cpu: read(f'/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_cur_freq') for cpu in (0,3,4,5,6,7)}}
    output.write(json.dumps(row) + '\n')
    output.flush()

def run(stage, commands, limit, output):
    processes, files = [], []
    started = time.monotonic()
    print('RUN ' + stage, flush=True)
    try:
        for i, cmd in enumerate(commands):
            handle = (OUT / f'{stage}-{i}.log').open('w')
            files.append(handle)
            processes.append(subprocess.Popen(cmd, cwd=ROOT, stdout=handle,
                stderr=subprocess.STDOUT, start_new_session=True))
        while any(p.poll() is None for p in processes):
            sample(stage, output)
            if time.monotonic() - started > limit: raise TimeoutError(stage)
            if int(read('/sys/class/thermal/thermal_zone0/temp') or 0) >= 80000:
                raise RuntimeError('temperature reached 80C guard')
            time.sleep(.25)
        if any(p.returncode for p in processes): raise RuntimeError('failed: ' + stage)
    finally:
        for p in processes:
            if p.poll() is None:
                try: os.killpg(p.pid, signal.SIGTERM)
                except ProcessLookupError: pass
                try: p.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(p.pid, signal.SIGKILL)
                    p.wait()
        for handle in files: handle.close()
    print('PASS ' + stage, flush=True)

summary = {'status': 'failed'}
try:
    (OUT/'softirqs-before.txt').write_text(read('/proc/softirqs'))
    (OUT/'dmesg-before.txt').write_text(subprocess.check_output(['dmesg'], text=True))
    with (OUT/'telemetry.jsonl').open('w') as output:
        for cpu, mib in ((7,64), (3,64), (7,256)):
            for op in ('read','write','copy'):
                run(f'cpu{cpu}-{mib}MiB-{op}', [['taskset','-c',str(cpu), './ddr_stream',op,str(mib),'2','3']], 30, output)
        for op in ('read','write','copy'):
            for trial in range(1,4):
                run(f'big4-{op}-trial{trial}', [['taskset','-c',str(cpu), './ddr_stream',op,'64','3','1'] for cpu in (4,5,6,7)], 20, output)
        run('tinymembench-cpu7', [['taskset','-c','7','./tinymembench/tinymembench']], 300, output)
    summary['status'] = 'completed'
except Exception as error:
    summary['error'] = str(error)
finally:
    (OUT/'softirqs-after.txt').write_text(read('/proc/softirqs'))
    (OUT/'dmesg-after.txt').write_text(subprocess.check_output(['dmesg'], text=True))
    (OUT/'summary.json').write_text(json.dumps(summary,indent=2) + '\n')
print(json.dumps(summary), flush=True)
raise SystemExit(0 if summary['status'] == 'completed' else 1)
