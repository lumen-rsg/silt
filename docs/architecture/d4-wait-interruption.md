# D4: caught signals interrupt Dash wait

## Contract and scope, 2026-09-13

Fourteen shared Linux/Silt checks now exercise Dash's `wait` builtin while the
shell is observably blocked in signal suspension. They add to the existing
20 job/trap cases. The signal is Ctrl-C through the controlling terminal, and
the waiting shell has an INT trap. The background children have separate job
groups and remain alive until explicitly terminated by the test or its trap.
No kernel, libc, Dash patch or rootfs change was needed for the covered behavior.

The [POSIX.1-2024 signal and error rules](https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html#tag_19_12)
require a caught signal to interrupt the wait builtin with a status above 128,
followed by execution of its trap. Dash represents SIGINT as 130 on both tested
platforms. These tests require the trap to see 130 and the next shell command
to see the same status even when the trap ends with `false`.

## Synchronization and assertions

`tools/dash_wait_cases.py` owns identical commands and ordered, exact output-line
assertions on both platforms. Each child announces `WT_CHILD_READY` from its
function body after fork initialization. The harness waits for that output
before beginning the wait/interrupt sequence.

`tools/wait_observer.py` observes the exact shell PID before every Ctrl-C:

- Linux: `/proc/PID/wchan` must identify `sigsuspend`, and `/proc/PID/status`
  must show SIGINT unblocked.
- Neva: GDB must see THREAD_BLOCKED, BLOCK_REASON_SIGNAL, a suspended mask,
  SIGINT unblocked, no active handler, and no current or retired CPU owner.
  Function calls and memory/register writes through GDB are disabled. The
  debugger detaches before the harness sends the terminal control character.

The gate therefore requires GDB with AArch64 and Python support even without
service crash injection. It never calls a kernel function or changes signal
state through the debugger. Each successful observation emits `WAIT_OBSERVED`
in the retained result log. It checks a stable blocked wait; it does not claim
coverage of every signal arrival during wait admission or a real-time latency
bound. GDB pauses QEMU while reading state, so this is software/runtime evidence,
not physical ARM qualification.

| Checks | Count | Required outcome |
|---|---:|---|
| Specific PID, no operands, multiple operands, job selector | 4 | Trap output precedes the next command; both see status 130 |
| Reap after those interruptions | 4 | SIGTERM succeeds and every selected child remains waitable with status 143 |
| Four successive waits on the same child | 4 | Each wait suspends with SIGINT unblocked, then returns 130 and executes its trap |
| Reap after repeated interruption | 1 | The surviving child can still be terminated and reaped |
| Trap terminates the child | 1 | First wait returns 130; the trap triggers child termination; second wait retrieves 143 |

Each complete run makes nine observed interrupt deliveries. Repeated delivery
checks SIGINT deliverability, not equivalence of every signal-mask bit. Ordinary reap tests
check SIGTERM success before waiting for 143, proving that the interrupt did not
already kill the child. The multiple-operand case reaps both children before the
next prompt can report and discard a completed job. Cleanup targets only the
recorded child PIDs, never a fallback group-zero PID.

## Failed fixture attempts and separate gap

The initial Linux assertion missed the first trap output because the PTY's `^C`
echo occupied the same line. The trap now emits a leading blank line after
saving its incoming status. An early multiple-operand test also returned to the
interactive prompt between reaps, allowing Dash to report/discard the second
job. Both reaps now occur in one command. These failures remain in
`build/d4-wait-linux-v1.*` and `build/d4-wait-linux-v2.*`.

The first target attempt correctly returned 130 and ran its trap, then failed
its `kill -0` liveness probe. Silt forwards signal zero to a kernel signal path
that rejects it. Signal-zero existence/permission probing remains a separate
POSIX gap; this item does not implement it or label it as passing. The final
liveness assertions use successful explicit termination followed by the retained
signal-death status. Logs remain in `build/d4-wait-target-v1.*`.

A later Linux run interrupted wait-all successfully, but timed out reaping its
child after SIGTERM (`build/d4-wait-linux-v4.*`). The child had not acknowledged
startup. Source inspection shows that it initially inherits the interactive
shell's ignored SIGTERM disposition until fork initialization resets it, so an
early termination signal can be lost. The exact child state was not captured
in that failed run; this is a plausible explanation, not fault-time attribution.
The final fixture requires an explicit child-ready handshake, removing that
startup window from the tested protocol. The failed attempt is not counted as a
clean pass. A pre-final Silt run passed all 145 checks without service crashes
(`build/d4-wait-target-v2.*`).

## Negative control

An isolated prepared Linux Dash tree in `build/d4-wait-negative/dash/` changes
only `waitcmd`'s interrupt return from `128 + pending_sig` to zero. The preceding
20 job/trap cases pass; after observed signal suspension, the first wait case
rejects trap status zero and return status zero. The control retains
`build.log`, `reference.log` and `reference.pty.log`. The original vendor tree
and tested Linux binary remain unchanged.

A separate observer control leaves the Linux shell at its interactive prompt.
The observer rejects that input wait after its bounded deadline, retaining
`wchan=wait_woken` in `build/d4-wait-final/observer-negative.log`. Blocking on
input is therefore not accepted as evidence that the wait builtin suspended.

## Verification

All five Silt host/reference gates pass. The frozen Linux build passed sixteen
consecutive runs of all 34 shared cases, including 144 observed wait interrupts.
The final frozen QEMU matrix passed **165/165 on 1/4/8/8/8/8 CPUs**, including
four consecutive eight-CPU runs. Across that matrix, all 84 new wait checks pass
with 54 observed signal-suspend states, 24 terminal cycles and 24 injected service
crashes. The preceding 20 shared job/trap cases also pass in every run.

| Run | Passed | Failed | Seconds |
|---|---:|---:|---:|
| rootfs-1-smp1 | 165 | 0 | 60.67 |
| rootfs-2-smp4 | 165 | 0 | 65.44 |
| rootfs-3-smp8 | 165 | 0 | 154.48 |
| rootfs-4-smp8 | 165 | 0 | 149.48 |
| rootfs-5-smp8 | 165 | 0 | 164.62 |
| rootfs-6-smp8 | 165 | 0 | 160.28 |

Exact inputs, SHA-256 records, Linux PTY transcripts, target UART output,
per-run debugger symbols and executed commands are retained in
`build/d4-wait-final/`. `results.json` records commands, return codes and timings;
`verified-results.md` records the final count/hash checks. Every frozen input,
current test script and per-run kernel/ttyd debugger image matches its recorded
hash. The current build outputs match the tested kernel, rootfs and Linux binary.

| Artifact | SHA-256 |
|---|---|
| Neva kernel | `558041de8305ccb37d2890d845a4d2e529400458661430c39610ade319a6dd6e` |
| Silt rootfs | `7c272db0aaae4d9107428e332892e0ddd9034bbebcf4da7af367fc5722a4551a` |
| Linux Dash | `e4b56cd9c33803f180f3e746a20299802c96f1bac9da9ccf5af77717216b24a0` |
| Shared wait cases | `837080dd21581636df84c44ef60c7542b6c23f0ee23df0cd26ce798ee0d404ef` |
| Wait observer | `0d17237b104c9c27856e4b970c4c570188522dc6c5e13819c3bebb48b0424c7e` |

The target command shape from the Silt checkout is:

```sh
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --terminal-cycles 4 --restart-tests \
  --uart-log build/wait-run.uart.log --gdb-log build/wait-run.gdb.log
```

`meson test -C build dash-linux-job-trap-reference` runs both shared Linux suites.


The kernel and rootfs remain the ASID-qualified Neva `227fcce` and Silt `0e00708`
artifacts. This item adds test execution on those same bytes; it does not rerun
or relabel the earlier normal/VM-failure kernel matrices as new evidence.

## Remaining work

The caught-SIGINT wait slice is bounded. Signals arriving before wait admission,
other caught signals, default/ignored dispositions and foreground-command trap
deferral are separate coverage. Multi-member foreground pipeline stop/continue
and trap ordering are next, followed by resource-exhaustion behavior. Signal-zero
probing, orphaned-group EIO and full controlling-terminal/PTY session semantics
remain open. D4 and full POSIX acceptance remain open.
