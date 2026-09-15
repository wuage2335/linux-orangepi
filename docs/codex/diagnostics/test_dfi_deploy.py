"""在临时启动目录验证部署前置校验与备份；不访问真实/boot。"""
import hashlib
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('deploy', Path(__file__).with_name('deploy_dfi_candidate.py'))
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)

class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.boot = self.root / 'boot'
        (self.boot / 'dtb/rockchip/overlay').mkdir(parents=True)
        self.base = self.boot / 'dtb/rockchip/rk3588s-orangepi-5-pro.dtb'
        self.base.write_bytes(b'original dtb')
        self.env = self.boot / 'orangepiEnv.txt'
        self.env.write_text('overlay_prefix=rk3588\noverlays=opi5pro-cam2\n')
        self.original = self.env.read_bytes()
        self.overlay = self.root / 'rk3588-dfi-clocks-candidate.dtbo'
        self.overlay.write_bytes(b'tested overlay')
        self.lock = self.root / 'lock'
        self.lock.touch()
        self.patches = []
        def mapped_path(value):
            text = str(value)
            return self.root / text.lstrip('/') if text.startswith('/boot') else Path(value)
        original_open = open
        def mapped_open(value, *args, **kwargs):
            if value == '/tmp/ov13850-camera-session.lock':
                value = self.lock
            return original_open(value, *args, **kwargs)
        for target, value in [
            ('Path', mapped_path), ('open', mapped_open),
            ('__file__', str(self.root / 'deploy_dfi_candidate.py')),
            ('BASE_SHA', hashlib.sha256(self.base.read_bytes()).hexdigest()),
            ('ENV_SHA', hashlib.sha256(self.original).hexdigest()),
            ('OVERLAY_SHA', hashlib.sha256(self.overlay.read_bytes()).hexdigest())]:
            item = patch.object(deploy, target, value, create=True)
            item.start()
            self.patches.append(item)
        for item in [patch.object(sys, 'argv', ['deploy', '--apply']),
                     patch.object(deploy.os, 'geteuid', return_value=0),
                     patch.object(deploy.os, 'sync'),
                     patch.object(deploy.subprocess, 'run')]:
            item.start()
            self.patches.append(item)
    def tearDown(self):
        for item in reversed(self.patches):
            item.stop()
        self.temp.cleanup()
    def test_backup_and_single_overlay_change(self):
        deploy.main()
        backups = list(self.boot.glob('camera-closeout-backup-*'))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / 'orangepiEnv.txt').read_bytes(), self.original)
        self.assertEqual(self.env.read_text(), 'overlay_prefix=rk3588\noverlays=opi5pro-cam2 opi5pro-dfi-clocks\n')
        self.assertEqual(self.base.read_bytes(), b'original dtb')
    def test_changed_configuration_blocks_write(self):
        self.env.write_text('user changed configuration')
        with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
            deploy.main()
        self.assertEqual(self.env.read_text(), 'user changed configuration')
        self.assertFalse(list(self.boot.glob('camera-closeout-backup-*')))
    def test_busy_camera_blocks_deployment(self):
        import fcntl
        with self.lock.open('r+') as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(RuntimeError, 'camera session still active'):
                deploy.main()
        self.assertEqual(self.env.read_bytes(), self.original)

if __name__ == '__main__':
    unittest.main()
