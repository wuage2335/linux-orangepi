#!/usr/bin/env python3
"""同文件系统原子替换 MPP 库，保留运行中进程已映射的旧 inode。"""
import os
from pathlib import Path
import shutil
import sys
import tempfile


def install(source, target):
    source, target = Path(source).resolve(), Path(target).resolve()
    libraries = sorted(source.glob('librockchip_mpp.so*'))
    if not libraries or not (source / 'librockchip_mpp.so.0').is_file():
        raise RuntimeError('missing MPP runtime library')
    if source == target:
        raise RuntimeError('source and destination must differ')
    target.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mpp-install-', dir=target) as stage:
        for library in libraries:
            shutil.copy2(library, Path(stage) / library.name, follow_symlinks=False)
        # 先替换实际文件，再发布符号链接。每次 rename 都是同文件系统原子操作。
        for library in sorted(libraries, key=lambda path: path.is_symlink()):
            os.replace(Path(stage) / library.name, target / library.name)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: install_mpp_runtime.py <source-lib-dir> <target-lib-dir>')
    install(sys.argv[1], sys.argv[2])
