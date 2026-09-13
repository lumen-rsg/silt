#!/usr/bin/env python3
"""Verify the normal /bin/sh session, executable scripts and nsh recovery."""
import argparse
from pathlib import Path
import shutil
import socket
import sys
from release_boot import await_session, SESSION_PROMPT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neva-source', type=Path, required=True)
    parser.add_argument('--neva-build', type=Path, required=True)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--smp', type=int, default=4)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expect-recovery', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    kernel = (args.output/'kernel.elf').resolve()
    tty = (args.output/'ttyd.elf').resolve()
    shutil.copy2(args.neva_build/'neva.elf', kernel)
    shutil.copy2(args.neva_build/'apps/ttyd.elf', tty)
    sys.path.insert(0, str(args.neva_source.resolve()/'tools'))
    sys.argv = ['test_runner.py', str(args.neva_build.resolve()), '--skip-build',
                f'--smp={args.smp}', '--nevfs-image='+str(args.rootfs.resolve())]
    import test_runner as runner
    runner.KERNEL_NAME = str(kernel)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    original = runner.qemu_command
    runner.qemu_command = lambda media: original(media)+['-gdb', f'tcp:127.0.0.1:{port}']
    session = runner.QemuSession()
    passed = 0

    def check(condition, name):
        nonlocal passed
        if not condition:
            raise AssertionError(name+': '+repr(session.output[-2500:]))
        passed += 1
        print('PASS '+name, flush=True)

    def command(text, prompt=SESSION_PROMPT):
        if len(text.encode()) > 126 or '\n' in text:
            raise ValueError('invalid terminal command')
        start = len(session.output)
        session.send(text)
        check(session.read_until(text.encode(), timeout=20, start_offset=start), 'complete command echo')
        after = session.output.index(text.encode(), start)+len(text)
        check(session.read_until(prompt, timeout=45, start_offset=after), 'prompt returned')
        return session.output[after:].replace(b'\r', b'')

    try:
        session.start()
        if args.expect_recovery:
            check(session.read_until(runner.PROMPT, timeout=120), 'failed startup restores recovery')
            check(b'nsh: session startup failed;' in session.output, 'startup failure diagnosed')
            check(b'B3_INITD_READY: PASS' in session.output, 'service graph remains ready')
            check(b'nsh: neva shell v1.0' in command('about', runner.PROMPT), 'recovery accepts commands')
        else:
            await_session(session)
            check(True, 'normal session starts after initd readiness')
            output = command('echo D5_ENV=$SHELL:$HOME:$USER:$LC_ALL; pwd; umask')
            check(b'D5_ENV=/bin/sh:/var/lib/shell:session:C' in output, 'startup environment')
            check(b'/var/lib/shell\n' in output and any(line in (b'22', b'0022') for line in output.splitlines()), 'startup cwd and umask')
            output = command('check-signals d5-exec; echo D5_STATUS=$?')
            check(b'D5_EXEC:' in output and b'D5_STATUS=0\n' in output, 'native exec and refusal assertions')
            check(b'D5_SCRIPT: argc=2 first=<> second=<two words> env=preserved' in output,
                  'script empty/quoted arguments and environment')
            output = command('/bin/sh /boot/d5-conformance.sh; echo D5_STATUS=$?')
            check(output.count(b'D5_CASE_PASS:') == 20 and b'D5_CASE_FAIL:' not in output
                  and b'D5_CONFORMANCE: 20 passed' in output and b'D5_STATUS=0\n' in output,
                  'twenty shared shell reference cases')
            output = command('/bin/sh -lc \'echo D5_LOGIN=$SHELL:$HOME\'')
            check(b'D5_LOGIN=/bin/sh:/var/lib/shell' in output, 'login profile')
            command('nsh', runner.PROMPT)
            check(b'nsh: neva shell v1.0' in command('about', runner.PROMPT), 'installed /bin/nsh')
            command('exit')
            command('exit', runner.PROMPT)
            check(b'nsh: neva shell v1.0' in command('about', runner.PROMPT), 'normal exit restores recovery')
            command('sh /boot/session.sh')
            check(b'D5_REENTER=/bin/sh' in command('echo D5_REENTER=$SHELL'), 'session can be entered again')
            command('exit', runner.PROMPT)
        print(f'D5 release acceptance: {passed} passed, 0 failed', flush=True)
        return 0
    except BaseException:
        from qemu_snapshot import snapshot
        (args.output/'failure.gdb.log').write_text(snapshot(kernel, port, tty))
        raise
    finally:
        (args.output/'uart.log').write_bytes(session.output)
        session.kill()


if __name__ == '__main__':
    raise SystemExit(main())
