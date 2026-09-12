"""Caught SIGINT while Dash is observably suspended inside its wait builtin."""

import re


def run_wait_cases(command, record):
    output = command("echo WT_PID=$$")
    match = re.search(rb"(?m)^WT_PID=([0-9]+)\r?$", output)
    if not match:
        raise AssertionError("missing exact wait-test shell PID")
    pid = int(match[1])
    command("unset p q")
    command("wt_hold() { echo WT_CHILD_READY; while :; do :; done; }")
    command("trap 's=$?; echo; echo WT_TRAP=$s; false' INT")

    def check(name, output, expected):
        lines = output.replace(b"\r", b"").splitlines()
        wanted = [item.encode() for item in expected]
        selected = [line for line in lines if line.startswith(b"WT_")]
        passed = selected == wanted
        record("wait/trap: " + name, passed, "" if passed else repr(output[-1500:]))
        if not passed:
            raise AssertionError("wait/trap case failed: " + name)

    def interrupt(selector, name):
        output = command(f"wait {selector}; echo WT_RETURN=$?",
                         interrupt_pid=pid)
        check(name, output, ["WT_TRAP=130", "WT_RETURN=130"])

    def reap(multiple=False):
        # Reap both before returning to the interactive prompt: Dash may report
        # and discard a completed un-waited job while displaying that prompt.
        text = "kill -TERM $p; echo WT_KILL=$?; wait $p; echo WT_REAP=$?"
        expected = ["WT_KILL=0", "WT_REAP=143"]
        if multiple:
            text = "kill -TERM $p $q; echo WT_KILL=$?; wait $p; echo WT_REAP=$?; wait $q; echo WT_REAP=$?"
            expected += ["WT_REAP=143"]
        check("interrupted children remain waitable", command(text), expected)

    try:
        for selector, name in (("$p", "specific PID"), ("", "all children"),
                               ("$p $q", "multiple operands"), ("%wt_hold", "job selector")):
            command("wt_hold & p=$!", ready=b"WT_CHILD_READY\n")
            if selector == "$p $q":
                command("wt_hold & q=$!", ready=b"WT_CHILD_READY\n")
            interrupt(selector, name)
            reap(selector == "$p $q")
            command("unset p q")

        command("wt_hold & p=$!", ready=b"WT_CHILD_READY\n")
        for i in range(4):
            interrupt("$p", f"repeated wait {i + 1} restores signal mask")
        reap()
        command("unset p")

        command("wt_hold & p=$!", ready=b"WT_CHILD_READY\n")
        command("trap 's=$?; echo; echo WT_TRAP=$s; kill -TERM $p' INT")
        output = command("wait $p; echo WT_RETURN=$?; wait $p; echo WT_REAP=$?", interrupt_pid=pid)
        check("trap terminates child without losing wait status", output,
              ["WT_TRAP=130", "WT_RETURN=130", "WT_REAP=143"])
        command("unset p")
    finally:
        command("trap - INT")
        # Never use a default PID of zero: it would target the shell's group.
        command('if test -n "$p"; then kill -KILL $p 2>/dev/null; wait $p 2>/dev/null; fi')
        command('if test -n "$q"; then kill -KILL $q 2>/dev/null; wait $q 2>/dev/null; fi')
        command("unset p q")
