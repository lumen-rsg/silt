"""Shared, curated dash job/trap cases for the pinned Linux build and Silt.

Commands are identical on both platforms. These are original regressions, not
an imported upstream test suite; provenance and boundaries are in the D4 record.
"""


def frame_command(command, done):
    suffix = (" " if command.rstrip().endswith("&") else "; ") + "printf '" + done + "\\n'"
    framed = command + suffix
    # ttyd's canonical edit buffer holds 128 bytes, independently of nsh's
    # shorter command buffer. Reject before a truncated line reaches Dash.
    if len(framed.encode()) > 128 or "\n" in framed or "\r" in framed:
        raise ValueError("interactive command exceeds ttyd's 128-byte line contract")
    return framed


def run_job_cases(command, record):
    def check(name, text, *expected):
        output = command(text).replace(b"\r", b"")
        lines = output.splitlines()
        passed = all(item.encode() in lines for item in expected)
        record("job/trap: " + name, passed, "" if passed else repr(output[-1500:]))
        if not passed:
            raise AssertionError("job/trap case failed: " + name)
        return output

    check("retained statuses in reverse wait order",
          "(exit 7) & a=$!; (exit 23) & b=$!; wait $b; echo JT_B=$?; "
          "wait $a; echo JT_A=$?", "JT_B=23", "JT_A=7")
    check("wait operands use final status",
          "(exit 9) & a=$!; (exit 21) & b=$!; wait $a $b; echo JT_MULTI=$?",
          "JT_MULTI=21")
    check("wait all zero and pinned-dash retained status",
          "(exit 9) & a=$!; (exit 21) & b=$!; wait; echo JT_ALL=$?; "
          "wait $a; echo JT_GONE=$?", "JT_ALL=0", "JT_GONE=9")
    check("unknown child status", "wait 999999; echo JT_UNKNOWN=$?", "JT_UNKNOWN=127")
    check("background pipeline last member",
          "(exit 9) | (exit 24) & p=$!; wait $p; echo JT_PIPE=$?", "JT_PIPE=24")
    check("trap action preserves interrupted status",
          "trap 'false' USR1; kill -USR1 $$; echo JT_TRAP_STATUS=$?; trap - USR1",
          "JT_TRAP_STATUS=0")
    check("EXIT trap sees and preserves status",
          "(trap 'printf \"JT_EXIT_IN=%s\\n\" \"$?\"; false' EXIT; exit 17); "
          "echo JT_EXIT_OUT=$?", "JT_EXIT_IN=17", "JT_EXIT_OUT=17")
    check("EXIT trap can replace status",
          "(trap 'exit 29' EXIT; exit 17); echo JT_EXIT_REPLACE=$?", "JT_EXIT_REPLACE=29")
    check("trap-only substitution preserves parent trap text",
          "trap ': JT_PARENT' USR1; v=$(trap); case $v in *JT_PARENT*) echo JT_SAVED;; esac",
          "JT_SAVED")
    check("ordinary substitution resets caught traps",
          "v=$(:; trap); case $v in *JT_PARENT*) :;; *) echo JT_RESET;; esac; trap - USR1",
          "JT_RESET")
    check("trap save and restore",
          "trap 'printf \"JT_RESTORED\\n\"' USR1; v=$(trap); trap - USR1; eval \"$v\"; "
          "kill -USR1 $$; trap - USR1", "JT_RESTORED")
    check("exec preserves ignored signals",
          "trap '' USR1; dash -c 'kill -USR1 $$; printf \"JT_IGNORED\\n\"'; "
          "echo JT_IGNORE_STATUS=$?; trap - USR1", "JT_IGNORED", "JT_IGNORE_STATUS=0")
    check("exec resets caught signals",
          "trap ': JT_CAUGHT' USR1; dash -c 'kill -USR1 $$; exit 99'; "
          "echo JT_EXEC_RESET=$(kill -l $?); trap - USR1", "JT_EXEC_RESET=USR1")

    # Two live, distinguishable jobs exercise selection without PID matching or
    # sleeps. SIGSTOP is unconditional, and jobs polls observe its completion.
    command("d4_alpha() { while :; do :; done; }; d4_beta() { while :; do :; done; }")
    command("d4_alpha & a=$!; d4_beta & b=$!")
    try:
        check("jobs-only substitution retains parent jobs",
              "v=$(jobs); case $v in *alpha*beta*|*beta*alpha*) echo JT_JOBS_SAVED;; esac",
              "JT_JOBS_SAVED")
        check("ordinary substitution clears parent jobs",
              "v=$(:; jobs); test -z \"$v\" && echo JT_JOBS_RESET", "JT_JOBS_RESET")
        check("current and previous job selection",
              "kill -STOP %+; s=$?; kill -STOP %-; echo JT_SELECT=$s:$?",
              "JT_SELECT=0:0")
        for _ in range(16):
            output = command("jobs").replace(b"\r", b"")
            if output.count(b"Stopped") == 2:
                break
        passed = output.count(b"Stopped") == 2 and b"d4_alpha" in output and b"d4_beta" in output
        record("job/trap: both selected groups stop", passed, "" if passed else repr(output))
        if not passed:
            raise AssertionError("selected jobs did not stop")
        check("prefix and substring job selection",
              "bg %d4_alpha; s=$?; bg %?beta; echo JT_BG=$s:$?", "JT_BG=0:0")
        check("signal death wait status",
              "kill -TERM $a $b; wait $a; echo JT_TERM_A=$?; "
              "wait $b; echo JT_TERM_B=$?", "JT_TERM_A=143", "JT_TERM_B=143")
    finally:
        # Best effort cleanup even if a case fails. Variables are the exact
        # children created above; no process-group-wide host cleanup is used.
        command("kill -KILL $a $b 2>/dev/null; wait $a $b 2>/dev/null; unset a b")
    check("jobs empty after reaping", "v=$(jobs); test -z \"$v\" && echo JT_EMPTY", "JT_EMPTY")
