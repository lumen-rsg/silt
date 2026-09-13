#!/usr/bin/env python3
"""Check exact installed command bytes, hardlinks, license and static ARM64 ABI."""
from pathlib import Path
import struct
import sys
from test_d5_image import Image

image = Image(sys.argv[1])
elf = Path(sys.argv[2]).read_bytes()
assert elf[:6] == b'\x7fELF\x02\x01'
assert struct.unpack_from('<HH', elf, 16) == (2, 183)
entry, program_offset = struct.unpack_from('<QQ', elf, 24)
program_size, count = struct.unpack_from('<HH', elf, 54)
executable = False
for index in range(count):
    kind, flags, offset, address, _, file_size, memory_size, _ = struct.unpack_from(
        '<IIQQQQQQ', elf, program_offset + index * program_size)
    assert kind not in (2, 3), 'dynamic/interpreter program header'
    if kind == 1:
        assert flags & 3 != 3, 'writable executable segment'
        if flags & 1 and address <= entry < address + memory_size:
            executable = True
assert executable
inode = image.lookup('/bin/coreutils')
assert image.contents(inode) == elf
assert struct.unpack_from('<H', image.inode(inode), 34)[0] == 11
assert struct.unpack_from('<H', image.inode(inode), 0)[0] & 0o777 == 0o755
for name in ('echo', 'basename', 'dirname', 'cat', 'head', 'tail', 'wc', 'pwd', 'printenv', 'yes'):
    assert image.lookup('/bin/' + name) == inode
root = Path(__file__).resolve().parent.parent
license_path = 'usr/share/licenses/coreutils/COPYING'
assert image.contents(image.lookup('/' + license_path)) == (root / 'rootfs' / license_path).read_bytes()
print('Coreutils: static ARM64 W^X image, exact bytes, eleven hardlinks and license PASS')
