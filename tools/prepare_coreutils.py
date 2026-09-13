#!/usr/bin/env python3
"""Verify the GNU release archive and apply the checked-in Silt port patches."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import urllib.request


def main():
    root = Path(__file__).resolve().parent.parent
    port = root / 'ports/coreutils'
    lock = json.loads((port / 'upstream.lock').read_text())
    archive = root / 'subprojects/packagecache' / f"coreutils-{lock['version']}.tar.xz"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        partial = archive.with_suffix('.part')
        urllib.request.urlretrieve(lock['source_url'], partial)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != lock['sha256']:
            raise ValueError('downloaded coreutils archive checksum mismatch')
        partial.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != lock['sha256']:
        raise ValueError('cached coreutils archive checksum mismatch')
    patches = sorted((port / 'patches').glob('*.patch'))
    marker = hashlib.sha256((port / 'upstream.lock').read_bytes()
                            + b''.join(p.read_bytes() for p in patches)).hexdigest()
    target = root / 'vendor' / f"coreutils-{lock['version']}"
    if (target / '.silt-prepared').is_file() and (target / '.silt-prepared').read_text() == marker:
        return
    if target.exists():
        raise FileExistsError(f'refusing to replace an incomplete or stale source tree: {target}')
    target.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.coreutils-', dir=root / 'subprojects') as tmp:
        with tarfile.open(archive) as source:
            source.extractall(tmp, filter='data')
        prepared = Path(tmp) / target.name
        for patch in patches:
            subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch)],
                           cwd=prepared, check=True)
        (prepared / '.silt-prepared').write_text(marker)
        prepared.rename(target)
    print(f"Prepared GNU coreutils {lock['version']} (SHA-256 verified)")


if __name__ == '__main__':
    main()
