#!/usr/bin/env python3
"""Compare GNU C1 stream commands against the pinned Linux build in QEMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from coreutils_stream_cases import cases
from release_boot import await_session, SESSION_PROMPT
from test_d5_image import Image


def signature(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f'{len(data)}:{value:x}'.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=Path('build/coreutils-reference/build/src'))
    parser.add_argument('--neva-source', type=Path, default=Path('../neva-microkernel'))
    parser.add_argument('--neva-build', type=Path, default=Path('../neva-microkernel/build-meson'))
    parser.add_argument('--rootfs', type=Path, default=Path('build/silt-rootfs.img'))
    parser.add_argument('--smp', type=int, default=4)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent.parent
    image = Image(args.rootfs)
    inode = image.lookup('/bin/coreutils')
    for name in ['cat', 'head', 'tail', 'wc']:
        assert image.lookup('/bin/' + name) == inode
    for name in ['lines', 'binary', 'long', 'empty']:
        assert image.contents(image.lookup('/boot/c1/' + name)) == (root/'rootfs/boot/c1'/name).read_bytes()
    env = dict(os.environ, LC_ALL='C')
    env.pop('POSIXLY_CORRECT', None)
    ref = args.reference.resolve()
    for name in ['cat','head','tail','wc']:
        assert subprocess.check_output([str(ref/name), '--version']).startswith(f'{name} (GNU coreutils) 9.11\n'.encode())
    sys.path.insert(0, str(args.neva_source.resolve()/'tools'))
    sys.argv = ['test_runner.py', str(args.neva_build.resolve()), '--skip-build',
                f'--smp={args.smp}', '--nevfs-image='+str(args.rootfs.resolve())]
    import test_runner as runner
    session = runner.QemuSession()
    passed = []
    complete = False
    hashes = {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
              [args.rootfs, args.neva_build/'neva.elf', args.neva_build/'disk.img', args.neva_build/'ext4.img']}
    hashes.update({str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in (args.neva_build/'apps').glob('*.elf')})
    hashes.update({str(ref/name): hashlib.sha256((ref/name).read_bytes()).hexdigest()
                   for name in ['cat', 'head', 'tail', 'wc']})
    bundle = args.neva_build/'boot.bundle'
    if bundle.is_file():
        hashes[str(bundle)] = hashlib.sha256(bundle.read_bytes()).hexdigest()

    def run(command, framed=True):
        text = command + ("; r=$?; printf '\\nC1_STATUS=%s\\n' \"$r\"" if framed else '')
        if len(text.encode()) > 126 or '\n' in text: raise ValueError('terminal frame too long: '+text)
        start = len(session.output)
        session.send(text)
        if not session.read_until(text.encode(), timeout=20, start_offset=start): raise TimeoutError('command echo')
        after = session.output.index(text.encode(), start)+len(text.encode())
        if not session.read_until(SESSION_PROMPT, timeout=60, start_offset=after): raise TimeoutError(command)
        body = session.output[after:].replace(b'\r\n', b'\n')
        assert body.startswith(b'\n'), body
        body = body[1:]
        if not framed: return body, 0
        match = re.search(rb'\nC1_STATUS=(\d+)\n', body)
        assert match, body
        return body[:match.start()], int(match[1])

    def compare(name, command, expected):
        body, status = run(command+' | check-pipes digest')
        assert status == 0, (name, body, status)
        lines = body.splitlines()
        assert b'C1_EXIT='+str(expected.returncode).encode() in lines, (name, body, expected.returncode)
        data = expected.stdout.replace(str(root/'rootfs').encode(), b'')
        if signature(data) not in lines:
            actual, _ = run(command + ' | check-pipes hex')
            raise AssertionError((name, body, signature(data), data[:200], actual))
        passed.append(name)
        print('PASS '+name, flush=True)

    try:
        session.start()
        await_session(session)
        assert not [line for line in session.output.splitlines() if re.search(rb': FAIL(?:\s|$)', line)]
        run('c1() { "$@" 2>/dev/null; echo C1_EXIT=$? >&2; }', framed=False)
        for case in cases():
            utility, arguments, source = case['utility'], case['arguments'], case['source']
            file = root/'rootfs/boot/c1'/source
            if case['pipe']:
                expected = subprocess.run([str(ref/utility)]+arguments, input=file.read_bytes(), capture_output=True, env=env, timeout=10)
                command = 'cat /boot/c1/'+source+' | c1 '+utility+' '+shlex.join(arguments)
            else:
                expected = subprocess.run([str(ref/utility)]+arguments+[str(file)], capture_output=True, env=env, timeout=10)
                command = 'c1 '+utility+' '+shlex.join(arguments+['/boot/c1/'+source])
            compare(case['name'], command.strip(), expected)
        for utility in ['cat','head','tail','wc']:
            for options in [[], ['-q']] if utility in ['head','tail'] else [[]]:
                operands=['/boot/c1/lines','/boot/c1/empty','/boot/c1/long']
                expected=subprocess.run([str(ref/utility)]+options+[str(root/'rootfs')+p for p in operands],capture_output=True,env=env)
                compare(utility+'-multi'+''.join(options), 'c1 '+utility+' '+shlex.join(options+operands), expected)
            body,status=run(utility+' /absent 2>/dev/null')
            assert status==1 and not body,(utility,body,status)
            passed.append(utility+'-missing')
            body,status=run(utility+' /boot/c1/lines >&- 2>/dev/null')
            assert status==1 and not body,(utility,body,status)
            passed.append(utility+'-closed-output')
            expected = subprocess.run([str(ref/utility), str(root/'rootfs/boot/c1/lines'), '-'],
                                      input=b'end\n', capture_output=True, env=env)
            compare(utility+'-explicit-stdin',
                    "printf 'end\\n' | c1 "+utility+' /boot/c1/lines -', expected)
            expected = subprocess.run([str(ref/utility), '/absent', str(root/'rootfs/boot/c1/lines')],
                                      capture_output=True, env=env)
            compare(utility+'-continues-after-missing',
                    'c1 '+utility+' /absent /boot/c1/lines', expected)
            body,status=run(utility+' /boot/c1 2>/dev/null')
            assert status==1, (utility,body,status)
            passed.append(utility+'-directory-error')
        for option in ['-n', '-b', '-s', '-A']:
            operands=['/boot/c1/lines', '/boot/c1/lines']
            expected=subprocess.run([str(ref/'cat'), option]+[str(root/'rootfs')+p for p in operands],
                                    capture_output=True,env=env)
            compare('cat-cross-file-'+option, 'c1 cat '+shlex.join([option]+operands), expected)
        posix_env = dict(env, POSIXLY_CORRECT='1')
        expected = subprocess.run([str(ref/'wc'), '-w'], input=(root/'rootfs/boot/c1/binary').read_bytes(),
                                  capture_output=True, env=posix_env)
        compare('wc-posix-whitespace', 'cat /boot/c1/binary | POSIXLY_CORRECT=1 c1 wc -w', expected)
        for option in ['--total=a', '--total=unknown', '--total=on']:
            expected = subprocess.run([str(ref/'wc'), option], input=b'', capture_output=True, env=env)
            compare('wc-option-'+option, 'c1 wc '+option+' </dev/null', expected)
        for version in ['199209', '200809', 'invalid', '9999999999999999999']:
            for utility in ['head', 'tail']:
                expected = subprocess.run([str(ref/utility), '+2'], input=b'a\nb\nc\n',
                                          capture_output=True, env=dict(env, _POSIX2_VERSION=version))
                compare(utility+'-posix-version-'+version,
                        "printf 'a\\nb\\nc\\n' | _POSIX2_VERSION="+version+' c1 '+utility+' +2', expected)
        for option in ['-f','-F','--follow=name','--retry','--pid=1','-s1','-2f']:
            body,status=run('tail '+option+' /boot/c1/lines 2>/dev/null')
            assert status==1 and not body,('tail-refusal',option,body,status)
            passed.append('tail-refuses-'+option)
        body,status=run('wc --files0-from=/boot/c1/lines 2>/dev/null')
        assert status==1 and not body,(body,status)
        passed.append('wc-refuses-file-list')
        body,status=run("echo original >/tmp/cu; cat /tmp/cu >>/tmp/cu 2>/dev/null")
        assert status==1 and not body,(body,status)
        body,status=run('cat /tmp/cu')
        assert body==b'original\n' and status==0,(body,status)
        passed.append('cat-same-file-refusal-preserves-data')
        run('c1short() { cat /boot/c1/binary | head -c1 >/dev/null; }', framed=False)
        body,status=run('i=0; while [ $i -lt 32 ]; do c1short || break; i=$((i+1)); done; test $i = 32')
        assert not body and status==0,(body,status)
        passed.append('early-consumer-exit-32-repetitions')
        body,status=run('check-pipes metadata')
        assert not body and status==0,(body,status)
        passed.append('pipe-file-metadata-and-descriptor-recovery')
        complete = True
        print(f'Coreutils C1: {len(passed)} passed, 0 failed', flush=True)
    finally:
        (args.output/'uart.log').write_bytes(session.output)
        (args.output/'result.json').write_text(json.dumps(dict(smp=args.smp,complete=complete,
                                                             passed=passed,hashes=hashes),indent=2)+'\n')
        session.kill()


if __name__ == '__main__':
    main()
