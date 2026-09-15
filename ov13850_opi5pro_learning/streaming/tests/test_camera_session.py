"""验证实际子进程的退出、超时和独占锁；无需摄像头。"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/camera_session.py'
if not SCRIPT.exists():
    SCRIPT = Path(__file__).with_name('camera_session.py')

class SessionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / 'video').touch()
        self.aiq = self.executable('aiq', 'import time\nwhile True: time.sleep(.1)\n')
        self.server = self.executable('server', '''import signal, time, sys
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
print('RTSP_SERVER_READY', flush=True)
while True: time.sleep(.1)
''')
        self.config = self.executable('config', "print('CONFIGURATION_OK')\n")
    def tearDown(self):
        self.tmp.cleanup()
    def executable(self, name, body):
        path = self.root / name
        path.write_text('#!/usr/bin/env python3\n' + body)
        path.chmod(0o755)
        return path
    def command(self, output='result'):
        return [sys.executable, str(SCRIPT), '--aiq-runner', str(self.aiq),
                '--server', str(self.server), '--configure', str(self.config),
                '--device', str(self.root / 'video'), '--lock-file', str(self.root / 'lock'),
                '--output', str(self.root / output), '--duration', '1',
                '--warmup', '0.1', '--startup-timeout', '2']
    def test_duration_reaps_children_and_preserves_logs(self):
        result = subprocess.run(self.command(), capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stderr)
        import json
        summary = json.loads((self.root / 'result/summary.json').read_text())
        self.assertEqual(summary['status'], 'completed')
        for pid in summary['child_pids']:
            self.assertFalse(Path('/proc', str(pid)).exists())
        self.assertTrue((self.root / 'result/telemetry.jsonl').is_file())
    def test_configuration_failure_reaps_aiq(self):
        self.config.write_text('#!/usr/bin/env python3\nraise SystemExit(7)\n')
        result = subprocess.run(self.command(), capture_output=True, text=True, timeout=8)
        self.assertNotEqual(result.returncode, 0)
        import json
        summary = json.loads((self.root / 'result/summary.json').read_text())
        self.assertIn('configure', summary['error'])
        for pid in summary['child_pids']:
            self.assertFalse(Path('/proc', str(pid)).exists())
    def test_early_aiq_exit_is_failure(self):
        self.aiq.write_text('#!/usr/bin/env python3\nraise SystemExit(0)\n')
        result = subprocess.run(self.command(), capture_output=True, text=True, timeout=8)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('rkaiq exited early', result.stderr)
    def test_second_session_cannot_take_lock(self):
        import fcntl
        with (self.root / 'lock').open('w') as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = subprocess.run(self.command(), capture_output=True, text=True, timeout=8)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('locked', result.stderr)
    def test_existing_output_is_not_overwritten(self):
        (self.root / 'result').mkdir()
        result = subprocess.run(self.command(), capture_output=True, text=True, timeout=8)
        self.assertNotEqual(result.returncode, 0)

    def test_board_build_refuses_active_session_lock(self):
        import fcntl
        script = SCRIPT.with_name('build_board.sh')
        with (self.root / 'lock').open('w') as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            env = dict(os.environ, CAMERA_LOCK_FILE=str(self.root / 'lock'))
            result = subprocess.run(['bash', str(script)], env=env,
                                    capture_output=True, text=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('camera session locked', result.stderr)

    def test_server_readiness_timeout_reaps_all_children(self):
        self.server.write_text('#!/usr/bin/env python3\nimport time\ntime.sleep(60)\n')
        result = subprocess.run(self.command(), capture_output=True, text=True, timeout=8)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RTSP startup timed out', result.stderr)

    def test_sigterm_shuts_down_cleanly(self):
        command = self.command()
        command[command.index('--duration') + 1] = '30'
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                path = self.root / 'result/server.log'
                if path.exists() and 'RTSP_SERVER_READY' in path.read_text():
                    break
                time.sleep(.05)
            else:
                self.fail('session failed to start')
            process.terminate()
            process.communicate(timeout=8)
            self.assertEqual(process.returncode, 130)
            import json
            summary = json.loads((self.root / 'result/summary.json').read_text())
            for pid in summary['child_pids']:
                self.assertFalse(Path('/proc', str(pid)).exists())
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

if __name__ == '__main__':
    unittest.main()
