#!/usr/bin/env python3
"""Compare GNU C2a inspection commands against the pinned Linux build in QEMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
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
    parser.add_argument('--environment-reference', type=Path, default=Path('build/coreutils-environment-reference'))
    parser.add_argument('--smp', type=int, default=4)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parent.parent
    image = Image(args.rootfs)
    inode = image.lookup('/bin/coreutils')
    for name in ['pwd', 'printenv', 'yes']:
        assert image.lookup('/bin/' + name) == inode
    env = dict(os.environ, LC_ALL='C')
    env.pop('POSIXLY_CORRECT', None)
    ref = args.reference.resolve()
    for name in ['pwd','printenv','yes']:
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
                   for name in ['pwd', 'printenv', 'yes']})
    hashes[str(args.environment_reference)] = hashlib.sha256(args.environment_reference.read_bytes()).hexdigest()
    bundle = args.neva_build/'boot.bundle'
    if bundle.is_file():
        hashes[str(bundle)] = hashlib.sha256(bundle.read_bytes()).hexdigest()

    def run(command, framed=True):
        text = command + ("; r=$?; printf '\\nC2_STATUS=%s\\n' \"$r\"" if framed else '')
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
        match = re.search(rb'\nC2_STATUS=(\d+)\n', body)
        assert match, body
        return body[:match.start()], int(match[1])

    def check(name, command, data=b'', expected_status=0, digest=False):
        body, status = run(command + (' | check-pipes digest' if digest else ''))
        if digest:
            assert status == 0 and b'C2_EXIT='+str(expected_status).encode() in body.splitlines(), (name, body, status)
            assert signature(data) in body.splitlines(), (name, body, data[:200])
        else:
            assert body == data and status == expected_status, (name, body, status, data, expected_status)
        passed.append(name)
        print('PASS '+name, flush=True)

    def finite(name, utility, arguments, command=None, **kwargs):
        result = subprocess.run([str(ref/utility)]+arguments, capture_output=True, env=env, timeout=5, **kwargs)
        check(name, command or ('c2 /bin/'+utility+' '+shlex.join(arguments)).rstrip(),
              result.stdout, result.returncode, digest=True)

    try:
        session.start()
        await_session(session)
        assert not [line for line in session.output.splitlines() if re.search(rb': FAIL(?:\s|$)', line)]
        run('c2() { "$@" 2>/dev/null; echo C2_EXIT=$? >&2; }', framed=False)
        check('cwd-native-contract', 'check-pipes cwd')
        # Linux uses a real directory tree; only the reference root prefix is
        # removed from pwd output. Logical PWD spellings remain significant.
        reference_root = str(root/'rootfs')
        for cwd in ['/', '/boot/c1', '/boot/d5', '/boot']:
            check('chdir-'+cwd, 'cd '+shlex.quote(cwd))
            for option in [[], ['-L'], ['-P'], ['-PL'], ['-LP'], ['--logical'], ['--physical'], ['ignored']]:
                result = subprocess.run([str(ref/'pwd')]+option, cwd=reference_root+cwd,
                                        env=dict(env, PWD=reference_root+cwd), capture_output=True, timeout=5)
                data = result.stdout.replace(reference_root.encode(), b'')
                if data == b'\n': data = b'/\n'
                check('pwd-'+cwd+'-'+str(option), 'PWD='+shlex.quote(cwd)+' c2 /bin/pwd '+shlex.join(option),
                      data, result.returncode, digest=True)
        check('chdir-logical-fixture', 'cd /boot/c1')
        for pwd in ['/boot//c1', '/boot/c1/', '/boot/c1/.', '/boot/c1/../c1', '/', '/tmp', 'relative', '']:
            for posix in [False, True]:
                refenv=dict(env, PWD=(reference_root+pwd if pwd.startswith('/') else pwd))
                if posix: refenv['POSIXLY_CORRECT']='1'
                options=[] if posix else ['-L']
                result=subprocess.run([str(ref/'pwd')]+options,cwd=reference_root+'/boot/c1',env=refenv,capture_output=True)
                data=result.stdout.replace(reference_root.encode(), b'')
                if data==b'\n':data=b'/\n'
                command=('POSIXLY_CORRECT=1 ' if posix else '')+'PWD='+shlex.quote(pwd)+' c2 /bin/pwd '+shlex.join(options)
                check('pwd-spelling-'+repr(pwd)+'-'+str(posix),command.strip(),data,result.returncode,digest=True)
        check('chdir-reset', 'cd /')
        for device in ['/dev/null','/dev/tty','/dev/ptmx']:
            check('pwd-root-rejects-device-'+device, 'PWD='+device+' /bin/pwd -L', b'/\n')
        for arguments in [['--bogus'], ['-Z'], ['--logical=x']]:
            finite('pwd-error-'+str(arguments),'pwd',arguments)
        helper=args.environment_reference.resolve()
        for arguments in [[], ['-0'], ['--null'], ['--nu'], ['ALPHA'], ['EMPTY'], ['DUP'],
                          ['DUP','DUP'], ['ALPHA','EMPTY','SPACED','LINES'], ['MISSING'],
                          ['ALPHA','MISSING','DUP'], [''], ['=unnamed'], ['MALFORMED'],
                          ['ALPHA=one'], ['--','-0'], ['-i'], ['--bogus']]:
            result=subprocess.run([str(helper),str(ref/'printenv'),'printenv']+arguments,
                                  capture_output=True,timeout=5)
            check('printenv-exact-env-'+str(arguments),'c2 check-pipes env printenv '+shlex.join(arguments),
                  result.stdout,result.returncode,digest=True)
        check('printenv-shell-export', "ALPHA='two words' /bin/printenv ALPHA",b'two words\n')
        check('printenv-unexported', 'unset SILT_PRIVATE; SILT_PRIVATE=x; /bin/printenv SILT_PRIVATE',b'',1)
        for utility in ['pwd','printenv','yes']:
            body,status=run('/bin/'+utility+' --version')
            assert status==0 and body.startswith((utility+' (GNU coreutils) 9.11\n').encode())
            passed.append(utility+'-version')
            body,status=run('/bin/'+utility+' --help')
            assert status==0 and body.startswith(b'Usage:')
            passed.append(utility+'-help')
            arguments=['ALPHA'] if utility=='printenv' else []
            if utility=='yes': arguments=['value']
            result=subprocess.run([str(ref/utility)]+arguments, env=dict(env,ALPHA='one'),
                                  stdout=None,stderr=subprocess.DEVNULL,
                                  preexec_fn=lambda: os.close(1),timeout=5)
            check(utility+'-closed-output', 'ALPHA=one /bin/'+utility+' '+shlex.join(arguments)+' >&- 2>/dev/null',
                  b'',result.returncode)
        for arguments in [['-n'], ['--bogus'], ['--help=x']]:
            finite('yes-error-'+str(arguments),'yes',arguments)
        # Read bounded bytes from the actual GNU producer, then close its pipe
        # and require SIGPIPE termination. Never buffer unbounded yes output.
        for arguments in [[], ['word'], ['', ''], ['one','two words'], ['--','-n'], ['--','--help'],
                          ['a\nb'], ['abc'*20], ['x'*110]]:
            for size in [1, 513, 4097]:
                producer=subprocess.Popen([str(ref/'yes')]+arguments,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,env=env)
                try:
                    data=producer.stdout.read(size)
                    producer.stdout.close()
                    assert producer.wait(timeout=5)==-13
                finally:
                    if producer.poll() is None: producer.kill(); producer.wait()
                # Multiline operands use a shell variable to fit one UART frame.
                if arguments==['a\nb']:
                    run("NL=$(printf 'a\\nb'); export NL",framed=False)
                    operand='"$NL"'
                elif arguments==['x'*110]:
                    run('LONG=$(head -c110 /boot/c1/long)',framed=False)
                    operand='"$LONG"'
                elif arguments==['abc'*20]:
                    run("LONG="+shlex.quote(arguments[0]),framed=False)
                    operand='"$LONG"'
                else: operand=shlex.join(arguments)
                check('yes-prefix-'+str(arguments)+'-'+str(size),
                      'c2 /bin/yes '+operand+' | head -c'+str(size),data,141,digest=True)
        check('yes-ignored-sigpipe', '(trap "" PIPE; c2 /bin/yes x) | head -c1',b'x',1,digest=True)
        run('LONG=$(head -c128 /boot/c1/long)',framed=False)
        check('yes-argv-budget-refusal','/bin/yes "$LONG" 2>/dev/null',b'',126)
        run('c2short() { /bin/yes x | head -c1 >/dev/null; }',framed=False)
        check('yes-early-exit-32-repetitions',
              'i=0; while [ $i -lt 32 ]; do c2short || break; i=$((i+1)); done; test $i = 32')
        check('descriptor-recovery', 'check-pipes metadata')
        check('multicall-pwd', 'coreutils pwd', b'/\n')
        check('multicall-printenv', 'ALPHA=one coreutils printenv ALPHA', b'one\n')
        complete=True
        print(f'Coreutils C2a: {len(passed)} passed, 0 failed',flush=True)
    finally:
        (args.output/'uart.log').write_bytes(session.output)
        (args.output/'result.json').write_text(json.dumps(dict(smp=args.smp,complete=complete,
                                                             passed=passed,hashes=hashes),indent=2)+'\n')
        session.kill()


if __name__=='__main__':
    main()
