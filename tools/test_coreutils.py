#!/usr/bin/env python3
"""Compare C0 command output/status on pinned GNU/Linux and fresh Silt QEMU media."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from coreutils_cases import cases
from release_boot import await_session, SESSION_PROMPT
from test_d5_image import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--neva-source', type=Path, default=Path('../neva-microkernel'))
    parser.add_argument('--neva-build', type=Path, default=Path('../neva-microkernel/build-meson'))
    parser.add_argument('--rootfs', type=Path, default=Path('build/silt-rootfs.img'))
    parser.add_argument('--smp', type=int, default=4)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    image = Image(args.rootfs)
    inode = image.lookup('/bin/coreutils')
    for command in ('echo', 'basename', 'dirname'):
        assert image.lookup('/bin/' + command) == inode
    for command in ('echo', 'basename', 'dirname'):
        version = subprocess.check_output([str(args.reference.resolve() / command), '--version'])
        assert version.startswith(f'{command} (GNU coreutils) 9.11\n'.encode())
    references = {}
    environment = os.environ.copy()
    environment['LC_ALL'] = 'C'
    environment.pop('POSIXLY_CORRECT', None)
    for case in cases():
        argv = case['args']
        command = shlex.join([str(args.reference.resolve() / argv[0])] + argv[1:])
        command = case.get('env', '') + ' ' + command + case.get('redirect', '')
        references[case['name']] = subprocess.run(command, shell=True, env=environment,
                                                 capture_output=True, timeout=10)
    sys.path.insert(0, str(args.neva_source.resolve() / 'tools'))
    sys.argv = ['test_runner.py', str(args.neva_build.resolve()), '--skip-build',
                f'--smp={args.smp}', '--nevfs-image=' + str(args.rootfs.resolve())]
    import test_runner as runner
    session = runner.QemuSession()
    passed = []
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
              [args.rootfs, args.neva_build / 'neva.elf', args.neva_build / 'boot.bundle', args.neva_build / 'disk.img', args.neva_build / 'ext4.img']
              if p.is_file()}
    hashes.update({str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in (args.neva_build / 'apps').glob('*.elf')})
    hashes.update({str(args.reference / c): hashlib.sha256((args.reference / c).read_bytes()).hexdigest()
                   for c in ('echo', 'basename', 'dirname')})

    def run(command):
        text = command + "; r=$?; printf '\\nCU_STATUS=%s\\n' \"$r\""
        if len(text.encode()) > 126 or '\n' in text:
            raise ValueError('terminal command exceeds frame: ' + text)
        start = len(session.output)
        session.send(text)
        if not session.read_until(text.encode(), timeout=20, start_offset=start):
            raise TimeoutError('missing complete command echo')
        after = session.output.index(text.encode(), start) + len(text.encode())
        if not session.read_until(SESSION_PROMPT, timeout=30, start_offset=after):
            raise TimeoutError('command did not return to Dash: ' + command)
        output = session.output[after:].replace(b'\r\n', b'\n')
        if not output.startswith(b'\n'):
            raise AssertionError('missing echo terminator: ' + repr(output))
        output = output[1:]
        match = re.search(rb'\nCU_STATUS=(\d+)\n', output)
        if not match:
            raise AssertionError('missing command status: ' + repr(output))
        return output[:match.start()], int(match[1])

    try:
        session.start()
        await_session(session)
        failures = [line for line in session.output.splitlines()
                    if re.search(rb': FAIL(?:\s|$)', line)]
        assert not failures, ('boot self-test failure', failures)
        for case in cases():
            argv = case['args']
            command = shlex.join(['/bin/' + argv[0]] + argv[1:])
            command = (case.get('env', '') + ' ' + command).strip() + case.get('redirect', '')
            # UART shares stdout/stderr; assert diagnostics separately from data.
            ref = references[case['name']]
            if ref.stderr:
                command += ' 2>/dev/null'
            binary = any(byte < 32 and byte not in (9, 10) for byte in ref.stdout)
            if binary:
                command += ' >/tmp/cu'
            actual, status = run(command)
            if binary:
                assert actual == b'', (case['name'], actual)
                encoded, reader_status = run('/bin/check-pipes hex </tmp/cu')
                assert reader_status == 0 and encoded.endswith(b'\n'), encoded
                actual = bytes.fromhex(encoded.decode().strip())
            assert status == ref.returncode, (case['name'], status, ref.returncode, actual)
            if 'prefix' in case:
                assert actual.startswith(case['prefix']), (case['name'], actual)
            else:
                assert actual == ref.stdout, (case['name'], actual, ref.stdout)
            passed.append(case['name'])
            print('PASS ' + case['name'], flush=True)
        for name, command, expected in [
            ('binary-pipeline', "/bin/echo -ne 'a\\0b' | /bin/check-pipes hex", b'610062\n'),
            ('pipeline', "/bin/echo -e 'one\\ntwo' | { read a; read b; echo \"$a:$b\"; }", b'one:two\n'),
            ('redirection', '/bin/echo saved >/tmp/cu; read a </tmp/cu; echo "$a"', b'saved\n'),
            ('substitution', 'x=$(/bin/basename /a/file.c .c); echo "$x"', b'file\n'),
            ('dispatch', '/bin/coreutils dirname a/b', b'a\n'),
        ]:
            actual, status = run(command)
            assert actual == expected and status == 0, (name, actual, status)
            passed.append(name)
            print('PASS ' + name, flush=True)
        print(f'Coreutils C0: {len(passed)} passed, 0 failed', flush=True)
    finally:
        (args.output / 'uart.log').write_bytes(session.output)
        (args.output / 'result.json').write_text(json.dumps(dict(smp=args.smp, passed=passed,
            expected=len(cases()) + 5, hashes=hashes), indent=2) + '\n')
        session.kill()


if __name__ == '__main__':
    main()
