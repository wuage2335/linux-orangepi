#!/usr/bin/env python3
"""单命令运行 RKAIQ、ISP 配置与 RTSP；保留日志，退出时回收本次子进程。"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time


def stop_process(process, grace=8):
    """关闭本次创建的进程组；最多等待 8 秒后强制回收。"""
    def send(sig):
        try:
            os.killpg(process.pid, sig)
            return True
        except ProcessLookupError:
            return False
    send(signal.SIGTERM)
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        process.poll()
        if not send(0):
            break
        time.sleep(.05)
    else:
        send(signal.SIGKILL)
    process.wait()
    return process.returncode


class DecodeProgress:
    """增量检查解码链日志；活着但不再出帧也应判为失败。"""
    def __init__(self, path, now, timeout=10):
        self.handle = Path(path).open(errors='replace')
        self.last_frame = now
        self.frames = 0
        self.timeout = timeout

    def check(self, now):
        frames = len(re.findall('decoded_frame.*chain', self.handle.read()))
        if frames:
            self.frames += frames
            self.last_frame = now
        if now - self.last_frame > self.timeout:
            raise RuntimeError('decoder stalled: no decoded frame for 10 seconds')

    def close(self):
        self.handle.close()


def snapshot():
    """采样原始温度与进程内存；无权限的指标保持缺失，绝不填零。"""
    temperatures = {}
    for node in Path('/sys/class/thermal').glob('thermal_zone*'):
        try:
            temperatures[node.name + ':' + (node / 'type').read_text().strip()] = int(
                (node / 'temp').read_text()) / 1000
        except (OSError, ValueError):
            pass
    return {'monotonic_s': time.monotonic(), 'unix_s': time.time(),
            'temperature_c': temperatures}


def record_command(output, name, command):
    """保存诊断原文和返回码，诊断权限不足不会伪装为验证通过。"""
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        (output / name).write_text(result.stdout + result.stderr)
        return result.returncode
    except (OSError, subprocess.TimeoutExpired) as error:
        (output / name).write_text(str(error))
        return -1


def run(args):
    lock = open(args.lock_file, 'a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise RuntimeError('camera session locked by another process')
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    children, handles, decoders = [], [], []
    summary = {'status': 'failed', 'child_pids': [], 'arguments': vars(args),
               'ddr_bandwidth': 'not measured: requires available DMC/DFI counter',
               'display_latency': 'not measured: requires optical display comparison'}
    stopping = False

    def on_signal(_signum, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    def start(name, command):
        handle = (output / (name + '.log')).open('w')
        handles.append(handle)
        process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        children.append((name, process))
        summary['child_pids'].append(process.pid)
        return process

    def check():
        if stopping:
            raise InterruptedError('stop requested')
        for name, process in children:
            if name in ('rkaiq', 'server') and process.poll() is not None:
                raise RuntimeError(f'{name} exited early: {process.returncode}')

    def delay(seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            check()
            time.sleep(.1)

    def client(index):
        process = start('client-' + str(index), [
            'gst-launch-1.0', '-v', 'rtspsrc',
            f'location=rtsp://127.0.0.1:{args.service}/live', 'protocols=tcp',
            'latency=30', '!', 'rtph264depay', '!', 'h264parse', '!', summary['decoder'],
            '!', 'identity', 'name=decoded_frame', 'silent=false', '!', 'fakesink',
            'sync=false'])
        progress = DecodeProgress(output / f'client-{index}.log', time.monotonic())
        decoders.append(progress)
        return process

    error = None
    try:
        for item in (args.server, args.configure):
            if not os.access(item, os.X_OK):
                raise RuntimeError(f'not executable: {item}')
        if not args.no_aiq and not os.access(args.aiq_runner, os.X_OK):
            raise RuntimeError(f'not executable: {args.aiq_runner}')
        if args.decode_check:
            for decoder in ('avdec_h264', 'mppvideodec', 'openh264dec'):
                if subprocess.run(['gst-inspect-1.0', decoder], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode == 0:
                    summary['decoder'] = decoder
                    break
            if 'decoder' not in summary:
                raise RuntimeError('missing H.264 decoder')
            for element in ('identity', 'rtspsrc'):
                if subprocess.run(['gst-inspect-1.0', element], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode:
                    raise RuntimeError('missing GStreamer element: ' + element)
        # fuser 不可用时依靠统一锁；不会停止其他会话或系统服务。
        if shutil.which('fuser') and subprocess.run(
                ['fuser', args.device], stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError('video device is already in use')
        summary['dmesg_before_rc'] = record_command(output, 'dmesg-before.log', ['dmesg'])
        summary['controls_before_rc'] = record_command(
            output, 'controls-before.log', ['v4l2-ctl', '-d', args.sensor, '--all'])
        if not args.no_aiq:
            start('rkaiq', [args.aiq_runner])
            delay(args.warmup)
        configure = start('configure', [args.configure])
        try:
            configure.wait(timeout=30)
        except subprocess.TimeoutExpired:
            raise RuntimeError('configure timed out')
        if configure.returncode:
            raise RuntimeError(f'configure failed: {configure.returncode}')
        check()
        start('server', [args.server, '--device', args.device, '--service', str(args.service),
                         '--mount', '/live', '--bitrate', '8000000', '--gop', '30',
                         '--queue-buffers', '2', '--mtu', '1200', '--mode', 'dmabuf'])
        deadline = time.monotonic() + args.startup_timeout
        while 'RTSP_SERVER_READY' not in (output / 'server.log').read_text(errors='replace'):
            check()
            if time.monotonic() >= deadline:
                raise RuntimeError('RTSP startup timed out')
            time.sleep(.1)
        print(f'CAMERA_SESSION_READY output={output}', flush=True)
        start_time = time.monotonic()
        decoder = client(1) if args.decode_check else None
        reconnected = False
        with (output / 'telemetry.jsonl').open('w') as telemetry:
            while not stopping:
                check()
                elapsed = time.monotonic() - start_time
                if args.duration and elapsed >= args.duration:
                    break
                if decoder and decoder.poll() is not None:
                    raise RuntimeError('decoder exited early')
                if decoder:
                    decoders[-1].check(time.monotonic())
                if decoder and not reconnected and elapsed >= args.duration / 2:
                    stop_process(decoder)
                    delay(1)
                    decoder = client(2)
                    reconnected = True
                sample = snapshot()
                if decoder:
                    sample['decoded_frames'] = decoders[-1].frames
                    sample['last_decoded_monotonic_s'] = decoders[-1].last_frame
                sample['processes'] = {}
                for name, process in children:
                    try:
                        sample['processes'][name] = {
                            'stat': Path(f'/proc/{process.pid}/stat').read_text().strip(),
                            'status': Path(f'/proc/{process.pid}/status').read_text()}
                    except OSError:
                        pass
                telemetry.write(json.dumps(sample) + '\n')
                telemetry.flush()
                delay(1)
        summary['elapsed_s'] = time.monotonic() - start_time
        summary['status'] = 'completed' if not stopping else 'interrupted'
    except InterruptedError:
        summary['status'] = 'interrupted'
    except Exception as failure:
        error = str(failure)
    finally:
        summary['exit_codes'] = {}
        summary['cleanup_errors'] = []
        for name, process in reversed(children):
            try:
                summary['exit_codes'][name] = stop_process(process)
            except Exception as failure:
                summary['cleanup_errors'].append(f'{name}: {failure}')
                error = error or 'process cleanup failed'
        for progress in decoders:
            progress.close()
        for handle in handles:
            handle.close()
        if summary['exit_codes'].get('server', 0) != 0:
            error = error or 'server did not shut down cleanly'
        summary['controls_after_rc'] = record_command(
            output, 'controls-after.log', ['v4l2-ctl', '-d', args.sensor, '--all'])
        summary['dmesg_after_rc'] = record_command(output, 'dmesg-after.log', ['dmesg'])
        power = Path('/sys/bus/i2c/devices/3-0010/power')
        time.sleep(.2)
        summary['pm'] = {}
        for key in ('runtime_status', 'runtime_usage'):
            try:
                summary['pm'][key] = (power / key).read_text().strip()
            except OSError:
                summary['pm'][key] = None
        server_log = output / 'server.log'
        if server_log.exists():
            text = server_log.read_text(errors='replace')
            summary['server_metrics'] = dict(re.findall(r'(\w+)=([\d.]+)', text))
            if args.decode_check:
                for index in (1, 2):
                    path = output / f'client-{index}.log'
                    count = len(re.findall('decoded_frame.*chain', path.read_text(
                        errors='replace'))) if path.exists() else 0
                    summary[f'client_{index}_decoded'] = count
                    if count < 30:
                        error = error or f'client {index} decoded fewer than 30 frames'
                metrics = summary['server_metrics']
                for key in ('dropped', 'timeouts'):
                    if metrics.get(key) != '0':
                        error = error or f'invalid server metric {key}={metrics.get(key)}'
                if 'RTSP_SERVER_STOPPED' not in text:
                    error = error or 'missing server shutdown summary'
                if summary['pm'] != {'runtime_status': 'suspended', 'runtime_usage': '0'}:
                    error = error or 'sensor PM did not return to suspended/0'
        if error:
            summary['status'], summary['error'] = 'failed', error
        (output / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n')
        lock.close()
    if error:
        print(error, file=sys.stderr)
        return 1
    return 0 if summary['status'] == 'completed' else 130


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', default=str(root / 'build/bin/v4l2_mpp_rtsp_server'))
    parser.add_argument('--configure', default=str(root.parent / 'scripts/configure_rkisp_1080p.sh'))
    parser.add_argument('--aiq-runner', default=str(Path.home() /
        'ov13850_opi5pro_learning/stage6/rkaiq-3a/runtime-v15/bin/run_rkaiq_local.sh'))
    parser.add_argument('--device', default='/dev/video11')
    parser.add_argument('--sensor', default='/dev/v4l-subdev2')
    parser.add_argument('--service', type=int, default=8554)
    parser.add_argument('--output', required=True, help='新的证据目录，禁止覆盖')
    parser.add_argument('--duration', type=float, default=0, help='秒；0 表示持续至 Ctrl+C')
    parser.add_argument('--warmup', type=float, default=3)
    parser.add_argument('--startup-timeout', type=float, default=15)
    parser.add_argument('--lock-file', default='/tmp/ov13850-camera-session.lock')
    parser.add_argument('--decode-check', action='store_true', help='本机解码并在中途重连；时长至少20秒')
    parser.add_argument('--no-aiq', action='store_true', help='固定曝光对照，不启动3A')
    args = parser.parse_args()
    if args.duration < 0 or args.warmup < 0 or args.startup_timeout <= 0:
        parser.error('time values out of range')
    if not 1 <= args.service <= 65535:
        parser.error('service out of range')
    if args.decode_check and args.duration < 20:
        parser.error('--decode-check requires --duration >= 20')
    try:
        return run(args)
    except Exception as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
