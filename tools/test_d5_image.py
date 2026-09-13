#!/usr/bin/env python3
"""Inspect the actual NevFS release image and rebuild it byte-for-byte."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


class Image:
    def __init__(self, image):
        self.data = Path(image).read_bytes()

    def inode(self, ino):
        group, index = divmod(ino - 1, 2048)
        table = struct.unpack_from('<I', self.data, 2 * 4096 + group * 32 + 8)[0]
        return self.data[table * 4096 + index * 128:table * 4096 + (index + 1) * 128]

    def contents(self, ino):
        inode = self.inode(ino)
        size = struct.unpack_from('<I', inode, 4)[0]
        blocks = list(struct.unpack_from('<12I', inode, 44))
        indirect = struct.unpack_from('<I', inode, 92)[0]
        if indirect:
            blocks += struct.unpack_from('<1024I', self.data, indirect * 4096)
        assert size <= len(blocks) * 4096
        return b''.join(self.data[block * 4096:(block + 1) * 4096]
                        for block in blocks[:(size + 4095)//4096])[:size]

    def lookup(self, path):
        ino = 2
        for name in path.strip('/').split('/'):
            directory = self.contents(ino)
            offset = 0
            while offset < len(directory):
                entry, length, name_length, kind = struct.unpack_from('<IHBB', directory, offset)
                assert length >= 8 and offset + length <= len(directory)
                if entry and directory[offset + 8:offset + 8 + name_length].decode() == name:
                    ino = entry
                    break
                offset += length
            else:
                raise FileNotFoundError(path)
        return ino


def build_command(neva_source, neva_build, silt_build, staging, output):
    root = Path(__file__).resolve().parent.parent
    manifest = root/'rootfs/manifest.json'
    command = [sys.executable, str(root/'tools/build_rootfs.py'), '--formatter',
               str(neva_source/'tools/mknevfs.py'), '--manifest', str(manifest),
               '--staging', str(staging), '--output', str(output)]
    for app in json.loads(manifest.read_text())['applications']:
        binary = neva_build/'apps/shell.elf' if app == 'nsh' else silt_build/(app+'.elf')
        command += ['--app', app, str(binary)]
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neva-source', type=Path, required=True)
    parser.add_argument('--neva-build', type=Path, required=True)
    parser.add_argument('--silt-build', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    image_path = args.silt_build/'silt-rootfs.img'
    image = Image(image_path)
    manifest = json.loads((root/'rootfs/manifest.json').read_text())
    assert len(image.data) == manifest['size_mib'] * 1024 * 1024
    dash = image.lookup('/bin/dash')
    assert image.lookup('/bin/sh') == dash
    assert struct.unpack_from('<H', image.inode(dash), 34)[0] == 2
    assert image.contents(dash) == (args.silt_build/'dash.elf').read_bytes()
    assert image.contents(image.lookup('/bin/nsh')) == (args.neva_build/'apps/shell.elf').read_bytes()
    executable_paths = ['bin/'+app for app in manifest['applications']] + ['bin/sh']
    for path in executable_paths + manifest['executable_paths']:
        assert struct.unpack_from('<H', image.inode(image.lookup(path)), 0)[0] & 0o777 == 0o755
    for path in ('/boot/session', '/etc/profile', '/boot/d5/noexec'):
        assert struct.unpack_from('<H', image.inode(image.lookup(path)), 0)[0] & 0o777 == 0o644
    with tempfile.TemporaryDirectory(prefix='silt-d5-image-') as tmp:
        rebuilt = Path(tmp)/'rootfs.img'
        subprocess.run(build_command(args.neva_source, args.neva_build, args.silt_build,
                                     root/'rootfs', rebuilt), check=True, stdout=subprocess.PIPE)
        assert rebuilt.read_bytes() == image_path.read_bytes()
    print('D5 image: shared sh/dash inode, exact executable bytes, modes and deterministic rebuild PASS')


if __name__ == '__main__':
    main()
