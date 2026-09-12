"""Shared foreground pipeline lifecycle and per-member trap ordering checks."""


def drive_terminal(expect, send, start, steps):
    """Await every acknowledgement in a phase before delivering its input."""
    for markers, data in steps:
        ends = [expect(marker, start) for marker in markers]
        if ends:
            start = max(ends)
        if data:
            send(data)


def run_pipeline_cases(command, record):
    def check(name, output, passed):
        record("pipeline: " + name, passed, "" if passed else repr(output[-2000:]))
        if not passed:
            raise AssertionError("pipeline case failed: " + name)

    command("pt_mark() { printf '\\nPT_%s_%s=%s\\n' \"$1\" \"$2\" \"$3\" >/dev/tty; }")
    command('pt_exit() { s=$?; pt_mark EXIT "$1" "$s"; false; }')
    command('pt_int() { pt_mark INT "$1" yes; exit "$2"; }')
    command("pt_run() { trap 'pt_mark CONT \"$pt_id\" yes' CONT; pt_mark READY \"$1\" yes; while :; do :; done; }")
    command("pt_install() { trap 'pt_int \"$pt_id\" \"$pt_status\"' INT; trap 'pt_exit \"$pt_id\"' EXIT; }")
    command('pt_stage() { pt_id=$1; pt_status=$2; pt_install; pt_run "$@"; }')
    ready = [b"\nPT_READY_A=yes\n", b"\nPT_READY_B=yes\n"]
    continued = [b"\nPT_CONT_A=yes\n", b"\nPT_CONT_B=yes\n"]

    for cycle, (left, right) in enumerate(((17, 23), (31, 7), (5, 0)), 1):
        label = f"cycle {cycle} "
        output = command(f"pt_stage A {left} | pt_stage B {right}; s=$?; echo; echo PT_STOP=$(kill -l $s)",
                         steps=[(ready, b"\x1a")])
        check(label + "Ctrl-Z stops foreground pipeline", output,
              b"PT_STOP=TSTP" in output.splitlines())

        def stopped():
            for _ in range(16):
                result = command("jobs -l")
                if b"Stopped" in result:
                    return result
            return result

        output = stopped()
        check(label + "stopped job lists both members", output,
              b"Stopped" in output and b"pt_stage A" in output and b"pt_stage B" in output)
        output = command("bg %+; echo PT_BG=$?", steps=[(continued, b"")])
        check(label + "bg continues both members", output,
              b"PT_BG=0" in output.splitlines() and all(marker in output for marker in continued))
        command("kill -STOP %+")
        output = stopped()
        check(label + "group STOP stops the pipeline again", output, b"Stopped" in output)
        output = command("fg %+; echo PT_RETURN=$?", steps=[(continued, b"\x03")])
        lines = [line for line in output.splitlines() if line.startswith(b"PT_")]
        expected = {b"PT_CONT_A=yes", b"PT_CONT_B=yes", b"PT_INT_A=yes", b"PT_INT_B=yes",
                    f"PT_EXIT_A={left}".encode(), f"PT_EXIT_B={right}".encode(),
                    f"PT_RETURN={right}".encode()}
        ordered = len(lines) == 7 and set(lines) == expected
        if ordered:
            for member, status in (("A", left), ("B", right)):
                ordered &= (lines.index(f"PT_CONT_{member}=yes".encode())
                            < lines.index(f"PT_INT_{member}=yes".encode())
                            < lines.index(f"PT_EXIT_{member}={status}".encode())
                            < lines.index(f"PT_RETURN={right}".encode()))
        check(label + "fg INT then EXIT traps precede last-member status", output, ordered)
        output = command('v=$(jobs); test -z "$v" && echo PT_EMPTY')
        check(label + "pipeline fully reaped", output, b"PT_EMPTY" in output.splitlines())

    # Dash propagates a foreground job's SIGINT to its waiting interactive
    # shell. The children reset this caught disposition during fork setup.
    command("trap 's=$?; echo; echo PT_SHELL_INT=$s; false' INT")
    command('pt_plain() { pt_mark READY "$1" yes; while :; do :; done; }')
    output = command("pt_plain A | pt_plain B; echo PT_SIGNAL_RETURN=$?",
                     steps=[(ready, b"\x03")])
    lines = [line for line in output.splitlines() if line.startswith(b"PT_")]
    check("foreground SIGINT reaches shell trap before next command", output,
          set(lines[:2]) == {b"PT_READY_A=yes", b"PT_READY_B=yes"}
          and lines[2:] == [b"PT_SHELL_INT=130", b"PT_SIGNAL_RETURN=130"])
    output = command('trap - INT; v=$(jobs); test -z "$v" && echo PT_SIGNAL_EMPTY')
    check("signal-terminated pipeline fully reaped", output, b"PT_SIGNAL_EMPTY" in output.splitlines())
