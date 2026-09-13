"""Real descriptor exhaustion and interactive recovery on Silt and Linux.

The launcher reserves 11..31; Dash owns 10 for job control. Only ordinary
shell redirections occupy 3..9. Linux uses a private 32-fd RLIMIT_NOFILE.
"""


def run_descriptor_cases(command, record, descriptors, *, consumer="cat"):
    def check(name, passed, output=b''):
        record('descriptor: ' + name, passed, '' if passed else repr(output[-2000:]))
        if not passed:
            raise AssertionError('descriptor case failed: ' + name)

    baseline = descriptors()
    command(f'fd_copy() {{ {consumer}; }}')
    command('fd_hold() { echo FD_READY; while :; do :; done; }')
    command('fd_stage() { while :; do :; done; }')
    command('fd_hold & fd_pid=$!', ready=b'FD_READY\n')
    for available in (0, 1, 2):
        for fd in range(3, 10 - available):
            command(f'exec {fd}>&1')
        for attempt in range(4):
            # Two free slots admit stage one; its retained read end leaves
            # only one slot for the next pipe. Larger pipelines exercise the
            # same refusal with additional unconstructed stages remaining.
            for stages in (3, 4):
                text = ' | '.join(['fd_stage'] * stages)
                output = command(text, rejected=True, reject_message=b'Pipe call failed\n')
                check(f'{available} free attempt {attempt + 1}.{stages} pipe rejected',
                      b'Pipe call failed\n' in output, output)
                output = command('echo FD_STATUS=$?; jobs')
                check('status and unrelated running job preserved',
                      b'FD_STATUS=2' in output.splitlines() and output.count(b'fd_hold') == 1
                      and b'fd_stage' not in output and b'Stopped' not in output, output)
        # exec with a failed open is a special-builtin error, but interactive
        # Dash must recover. No completion marker should mask its status.
        if available == 0:
            output = command('exec 3>/dev/null', rejected=True,
                             reject_message=b'Too many open files\n')
            check('full table open rejected', b'Too many open files\n' in output, output)
            output = command('echo FD_STATUS=$?')
            check('failed redirection returns status two', b'FD_STATUS=2' in output.splitlines(), output)
        if available <= 1:
            # Borrowed duplicate sources must survive, too. At one free fd,
            # /dev/null opens successfully before saving stdout is refused.
            text = ': >/dev/null' if available else ': 1>&2'
            command(text, rejected=True, reject_message=b'Too many open files\n')
            output = command('echo FD_STATUS=$?; echo FD_OUTPUT=ok')
            check('failed redirection preserves stdout and status',
                  b'FD_STATUS=2' in output.splitlines() and b'FD_OUTPUT=ok' in output.splitlines(), output)
        for fd in range(3, 10 - available):
            command(f'exec {fd}>&-')
        check(f'{available} free descriptor capacity restored', descriptors() == baseline)
        output = command('printf "FD_RECOVERED\\n" | fd_copy')
        check(f'{available} free subsequent pipeline succeeds', b'FD_RECOVERED' in output.splitlines(), output)
    output = command('kill -TERM $fd_pid; wait $fd_pid; echo FD_REAP=$?')
    check('unrelated job reaps normally', b'FD_REAP=143' in output.splitlines(), output)
    output = command('jobs')
    check('job table empty after recovery', b'fd_hold' not in output and b'fd_stage' not in output, output)
    check('final descriptor capacity matches baseline', descriptors() == baseline)
