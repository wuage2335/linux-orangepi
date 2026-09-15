"""动态库更新不得截断已映射文件；在独立目录验证 inode 替换语义。"""
from pathlib import Path
import mmap
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/install_mpp_runtime.py'
if not SCRIPT.exists():
    SCRIPT = Path(__file__).with_name('install_mpp_runtime.py')

class RuntimeInstallTests(unittest.TestCase):
    def test_replaces_inode_without_changing_existing_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, target = root / 'source', root / 'target'
            source.mkdir()
            target.mkdir()
            (source / 'librockchip_mpp.so.0').write_bytes(b'new' * 4096)
            (source / 'librockchip_mpp.so').symlink_to('librockchip_mpp.so.0')
            old = target / 'librockchip_mpp.so.0'
            old.write_bytes(b'old' * 4096)
            with old.open('rb') as handle, mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ) as mapping:
                original_inode = old.stat().st_ino
                result = subprocess.run([sys.executable, str(SCRIPT), str(source), str(target)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(mapping[:], b'old' * 4096)
                self.assertNotEqual(old.stat().st_ino, original_inode)
                self.assertEqual((target / 'librockchip_mpp.so').read_bytes(), b'new' * 4096)

if __name__ == '__main__':
    unittest.main()
