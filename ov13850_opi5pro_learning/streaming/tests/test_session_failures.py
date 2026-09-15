import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

script = Path(__file__).resolve().parents[1] / 'scripts/camera_session.py'
if not script.exists():
    script = Path(__file__).with_name('camera_session.py')
spec = importlib.util.spec_from_file_location('session', os.environ.get('SESSION_MODULE', str(script)))
session = importlib.util.module_from_spec(spec)
spec.loader.exec_module(session)

class FailureTests(unittest.TestCase):
    def test_stall_after_initial_frames_is_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'client.log'
            path.write_text('decoded_frame chain\n' * 30)
            progress = session.DecodeProgress(path, 0)
            try:
                progress.check(0)
                self.assertEqual(progress.frames, 30)
                with self.assertRaisesRegex(RuntimeError, 'stalled'):
                    progress.check(11)
            finally:
                progress.close()

    def test_exited_group_race_does_not_abort_cleanup(self):
        process = subprocess.Popen([sys.executable, '-c', 'pass'], start_new_session=True)
        process.wait()
        with patch.object(session.os, 'killpg', side_effect=ProcessLookupError):
            self.assertEqual(session.stop_process(process), 0)

    def test_orphan_in_owned_group_is_killed(self):
        code = '''import subprocess, sys, time
p = subprocess.Popen([sys.executable, '-c', 'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)'], stdout=subprocess.DEVNULL)
time.sleep(.2)
print(p.pid, flush=True)
'''
        process = subprocess.Popen([sys.executable, '-c', code], stdout=subprocess.PIPE,
                                   text=True, start_new_session=True)
        child = int(process.stdout.readline())
        process.wait()
        try:
            session.stop_process(process, grace=.2)
            import time
            time.sleep(.1)
            path = Path(f'/proc/{child}/stat')
            self.assertTrue(not path.exists() or path.read_text().rsplit(')', 1)[1].split()[0] == 'Z')
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.stdout.close()

if __name__ == '__main__':
    unittest.main()
