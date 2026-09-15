#!/usr/bin/env python3
"""在确认串口可回滚后，由用户sudo执行；只安装最小DFI时钟overlay并备份配置。"""
import argparse
import fcntl
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

BASE_SHA = '5487d0d0b74cbd59c17846c5adbb6372049b7615313b6abd1d4ceca74604e409'
ENV_SHA = '0193e2eee6a6a89cdd8e16a44bbec82054779d7ece1b6b57701e1de6f3716d9b'
OVERLAY_SHA = '3a55fd7e2989a97319cc5018d322935e7c37f4c5971f7b7c7dff845fa8b8c21e'

def check(path, expected):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError('hash mismatch: ' + str(path))

def atomic_copy(source, target, mode):
    fd, name = tempfile.mkstemp(prefix='.camera-dfi-', dir=target.parent)
    os.close(fd)
    try:
        shutil.copyfile(source, name)
        os.chmod(name, mode)
        os.replace(name, target)
    finally:
        if os.path.exists(name):
            os.unlink(name)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--wait', action='store_true', help='最多等待一小时，直到当前摄像头会话结束')
    args = parser.parse_args()
    if not args.apply:
        parser.error('requires --apply; run only after serial rollback is available')
    if os.geteuid() != 0:
        raise RuntimeError('run with sudo in your board terminal')
    # root打开已有普通用户锁时不用O_CREAT，兼容fs.protected_regular保护。
    with open('/tmp/ov13850-camera-session.lock', 'r+') as lock:
        deadline = time.monotonic() + 3600
        while True:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if not args.wait or time.monotonic() >= deadline:
                    raise RuntimeError('camera session still active; no boot files changed')
                time.sleep(1)
        base = Path('/boot/dtb/rockchip/rk3588s-orangepi-5-pro.dtb')
        env = Path('/boot/orangepiEnv.txt')
        source = Path(__file__).with_name('rk3588-dfi-clocks-candidate.dtbo')
        target = Path('/boot/dtb/rockchip/overlay/rk3588-opi5pro-dfi-clocks.dtbo')
        check(base, BASE_SHA)
        check(env, ENV_SHA)
        check(source, OVERLAY_SHA)
        lines = env.read_text().splitlines()
        indexes = [i for i, line in enumerate(lines) if line.startswith('overlays=')]
        if len(indexes) != 1 or 'overlay_prefix=rk3588' not in lines:
            raise RuntimeError('unexpected boot overlay configuration')
        with tempfile.TemporaryDirectory(prefix='camera-dfi-check-') as scratch:
            subprocess.run(['fdtoverlay', '-i', str(base), '-o', scratch + '/merged.dtb', str(source)], check=True)
            backup = Path('/boot') / time.strftime('camera-closeout-backup-%Y%m%d-%H%M%S')
            backup.mkdir(exist_ok=False)
            shutil.copy2(env, backup / 'orangepiEnv.txt')
            if target.exists():
                shutil.copy2(target, backup / target.name)
            lines[indexes[0]] += ' opi5pro-dfi-clocks'
            changed = Path(scratch) / 'orangepiEnv.txt'
            changed.write_text('\n'.join(lines) + '\n')
            atomic_copy(source, target, 0o644)
            atomic_copy(changed, env, 0o644)
            os.sync()
        print('DFI_CANDIDATE_INSTALLED')
        print('backup=' + str(backup))
        print('No reboot performed. Inspect configuration, then reboot from your terminal.')
        print('Rollback: sudo cp ' + str(backup / 'orangepiEnv.txt') + ' /boot/orangepiEnv.txt && sudo reboot')

if __name__ == '__main__':
    main()
