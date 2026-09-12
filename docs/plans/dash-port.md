# dash port plan

## Goal

Provide upstream dash as Silt's `/bin/sh`, with POSIX shell-language behavior
and capability-safe process, filesystem, descriptor, and terminal semantics.
Keep `/bin/nsh` as the recovery and diagnostics shell.

## Baseline

- Upstream: dash 0.5.13.5, commit
  `037bbdfd330017c368caf6242f977974123239b5`.
- Target: static AArch64 ELF for Neva EL0.
- First build mode: `SMALL=1`, `JOBS=0`, no libedit.
- No upstream source snapshot or downstream fork is committed; Silt owns a
  reproducible patch set, configuration, and adapters.

## Status

As of 2026-08-16, D0 through D3 acceptance are complete. A clean,
checksum-verified source preparation applies the ordered Silt patch set. The
Arm GNU cross build produces the source-readiness archive and links it with a
Silt-owned libc baseline into `dash.elf`; a separate audit rejects unresolved
symbols in the process image. The image is installed as `/bin/dash` for the D3
QEMU gate. Fork, exec, wait, pipe, and pipeline-group entry points are
implemented by the Silt personality over capability-selected Neva operations.

## Milestone D0: reproducible source and compile gate

- Pin the release URL, SHA-256 digest, commit, and license.
- Generate upstream derived sources with native build tools.
- Compile every dash translation unit with the Neva ARM64 compiler into a
  freestanding static archive.
- Treat successful archive construction only as source/headers readiness, not
  runtime evidence.

## Milestone D1: process image and libc baseline

- Move or re-home `libsilt` under the Silt project while keeping Neva's raw ABI
  shim separately versioned.
- Define real `argc`, `argv`, `envp`, auxiliary startup metadata, and bounded
  environment storage across spawn/fork/exec. `SYS_EXEC` now accepts a bounded
  NUL-separated vector, preserving whitespace within arguments.
- Implement `errno`, allocation, ctype, integer conversion, formatted I/O, and
  the string/memory surface required by dash.
- Link dash and run built-in-only commands: `:`, `true`, `false`, `printf`,
  assignments, quoting, parameter expansion, arithmetic, and control flow.

Acceptance: `dash -c` passes a curated built-in language suite in QEMU without
opening files, forking, or acquiring undeclared capabilities.

## Milestone D2: descriptor and filesystem personality

- Define a process-local descriptor table whose entries retain typed Neva
  capabilities plus access mode, offset policy, and descriptor flags.
- Implement `openat`, `close`, `read`, `write`, `lseek`, `fstat`, `stat`,
  `fcntl`, `dup`, `dup2`, `getcwd`, `chdir`, `opendir`, and `readdir`.
- Preserve descriptor state across fork and apply `FD_CLOEXEC` atomically at
  exec commit. Silt's table is fork-copyable and mirrors all-descriptor
  `FD_CLOEXEC` state into Neva handle flags; D3 owns survivor reconstruction
  when it introduces `execve`.
- Implement `/dev/null` and `/dev/tty` through explicit namespace/TTY grants.

Acceptance: dash reads scripts, performs redirection, changes directories, and
runs `test` without ambient VFS or fixed-handle assumptions.

Evidence: the rootfs QEMU gate exercises those operations plus globbing,
private `/tmp`, `/dev/null`, explicit `/dev/tty`, and the `/etc/shadow` data
ceiling at 1, 4, and 8 vCPUs.

## Milestone D3: execution, wait, and pipelines

- Implement `fork`, `execve`, `_exit`, `waitpid`, and `kill` adapters over
  capability-selected Neva operations.
- Add a bounded pipe object or service with independent read/write endpoints,
  EOF on last-writer close, `SIGPIPE`/`EPIPE`, blocking wakeup, cancellation,
  and rollback on partial construction.
- Form one ProcessGroup per pipeline and attach every stage before resume.
- Make all setup failures close transferred handles and reap stopped children.

Acceptance: external commands, subshells, `&&`, `||`, command substitution,
redirection, and multi-stage pipelines pass at 1, 4, and 8 vCPUs.

Evidence: the rootfs QEMU gate covers external executable-VMO replacement,
environment and descriptor reconstruction, built-in and external pipe stages,
command substitution, file redirection across exec, and pipeline status. Neva
starts Silt forks suspended; sessiond creates or joins the caller-selected job
group before Silt resumes any stage.

## Milestone D4: signals and interactive job control

Status: implemented bring-up slice; acceptance remains open. `JOBS=1` now
exercises real action masks, atomic signal suspension, capability-backed job
groups, and ttyd-backed terminal I/O. See
`docs/architecture/signals-and-terminal.md` for the supported subset and
remaining restart, hard-kill pipe-lifecycle, and conformance work.

- Complete `sigaction`, signal masks, `sigsuspend`, and interrupted-wait rules.
- Implement `isatty`, the required `termios` subset, `tcgetpgrp`, and
  capability-authorized `tcsetpgrp` through sessiond/ttyd.
- Enable dash `JOBS=1`; validate foreground/background pipelines, `jobs`, `fg`,
  `bg`, `^C`, `^Z`, `SIGTTIN`, `SIGTTOU`, and terminal restoration.

Acceptance: upstream job tests and Neva's UART/PTY interaction gates pass,
including service restart and consecutive clean SMP-8 runs.

Bring-up verification, 2026-09-05: the final 1/4/8/8-vCPU sequence passed all
82 rootfs checks in its first three runs. The repeat SMP-8 run failed to reach
`B3_INITD_READY`: blockd faulted in `submit` during the active-I/O reset test,
and the recovery shell stopped responding. The final sequence is therefore
**failed**, not consecutive-clean acceptance. The three Meson checks (required
symbols, linked closure, Linux PTY interrupt regression) pass. The full local
sequence is retained in ignored `build/d4-rootfs-matrix.log`.

Other runs also exposed early filesystem-provider parse failures. Their cause
has not been attributed to or excluded from these changes. An isolated build
of Neva `ce1ea4e` passed four consecutive 101-check SMP-8 runs; that comparison
does not prove an intermittent failure cannot exist in the baseline. Diagnose
the failed paths before committing or closing D4.

Follow-up, 2026-09-12: a live SMP-8 snapshot identified a pre-existing Neva
scheduler/Process lock inversion during exit notification. Neva now releases
the Process lock before signaling its state Event; a deterministic native
regression rejects the old ordering. The rootfs **1/4/8/8/8/8-vCPU** sequence
passed all **82 checks on every run**, including four consecutive SMP-8 runs
(`build/d4-reaper-rootfs-matrix.log`). Neva's 33 host tests and Silt's three
host checks pass. See Neva's `docs/architecture/d4-process-exit-deadlock.md` for
the captured cycle and artifact identities. The reset-time blockd fault also
occurs in successful boots; historical filesystem parse failures remain
unattributed. That run left active-job service restart, hard-kill pipe
reclamation, and the remaining signal/job conformance scope open.

Active-job follow-up, 2026-09-12: UART handoffs now go through sessiond, which
retains the selected group for ttyd recovery. A replacement sessiond recovers
ttyd's exact live owner through JobControl enumeration instead of restoring
nsh. Client UART remotes cannot bypass the manager's handoff path. Recovery
enumeration covers the full bounded membership set, and recovered managers no
longer allocate an unused child domain.

The opt-in `tools/test_rootfs.py --restart-tests` gate crashed ttyd/sessiond
with a blocked foreground reader, two stopped background jobs, and later a
busy foreground job. All **87 checks passed at 1/4/8/8/8/8 vCPUs**, including
24 injected crashes and four consecutive SMP-8 runs. Full logs are in ignored
`build/d4-service-restart-matrix.log` and `build/d4-service-restart-*.uart.log`.
Neva's 1/4/8-vCPU regression passed 94/97/101 checks; its 33 host tests and
Silt's three host checks pass. Neva's `docs/architecture/d4-active-job-recovery.md`
records the authority contract, fault-injection mechanism and artifact hashes.

This closes separate-service **foreground ownership** recovery, not persistent
termios/queues, simultaneous service loss, or arbitrary in-flight calls. Next:
uncatchable-death pipe endpoint reclamation. D4 remains open for that work and
the remaining signal/job conformance cases.

Pipe-lifetime follow-up, 2026-09-12: Silt now uses Neva anonymous byte-stream
reader/writer capabilities instead of libc-owned SHM counters and a userspace
spinlock. Capability teardown handles SIGKILL, faults, raw exit, fork rollback,
revocation, and death before a suspended child starts. Dup/fork/exec/CLOEXEC
no longer require separate pipe registrations; the Silt exec handoff is V2.
Buffered bytes drain before EOF; last-reader death produces SIGPIPE/EPIPE.
The transport has a 4096-byte ring, 512-byte atomic writes, and broadcast
readiness snapshots with subscribe/retry/wait ordering. Nonblocking status
flags remain explicitly unsupported rather than silently ignored.

The new multi-reader regression also exposed a pre-existing Neva three-way
COW reservation bug. Completing the first copy released credit needed by the
remaining two mappings. A native regression rejects the old PMM calculation;
the fix additionally covers mapper death and fork rollback during a claim.

The final **1/4/8/8/8/8-vCPU** Silt sequence passed **89 checks on every run**,
including the forced-death/atomicity/exhaustion suite, 24 service crashes and
four consecutive SMP-8 passes (`build/d4-pipe-repeat-matrix.log` and
`build/d4-pipe-repeat-{1..6}.uart.log`). Neva passed **34 host tests**, its
**94/97/101-check** normal QEMU matrix, and its separate fault-injection
kernel's **89/92/96-check** VM rollback matrix. Silt's three host/reference
checks pass. Neva's `docs/architecture/d4-byte-stream-lifetime.md` records
mechanism boundaries, commands, artifact identities, and exact limitations.

An earlier combined run passed all pipe checks but timed out starting a later
background job before service injection (`build/d4-pipe-rootfs-matrix.log`,
`build/d4-pipe-final-1.uart.log`). Its cause remains unattributed despite the
subsequent clean matrix. The bounded pipe-lifetime item is implemented; D4
remains open. Next: background termios mutation and ignored/blocked SIGTTOU
policy, then broader job/trap and interrupted-operation resource cleanup.
Unhandled synchronous faults also still report exit 1 rather than a typed
termination signal. No full POSIX, D4, or hardware closure is claimed.

Background-terminal follow-up, 2026-09-12: ttyd now checks background attribute
changes regardless of TOSTOP, while writes retain the TOSTOP condition. UART
foreground changes use the same rule through sessiond. A new conditional Neva
group-signal operation atomically checks the caller's ignored/blocked state;
suppressed SIGTTOU permits the operation without generating pending signals or
stopping peers. Default stops retry after continuation; caught signals produce
EINTR without mutation. Missing or wrong-group capabilities cannot authorize a
background attribute change. The kernel supplies signal machinery, not terminal
policy. nsh ignores SIGTTOU so it can reclaim the terminal like dash.

The new 24-case `check-terminal` fixture covers writes, attributes, UART
foreground handoff, signal dispositions, both TOSTOP settings and negative
authority cases. Same-group peers use Event handshakes to verify suppressed
signals do not stop them. Interactive dash checks additionally verify that a
background setter stops before mutation, stops again after `bg`, and completes
after `fg`. Exact write-result checks exposed and fixed a separate libc defect:
terminal service reply copyback targeted read-only string literals. Writes now
stage their payload in writable memory.

The final **1/4/8/8/8/8-vCPU** sequence passed **96 checks on every run**, with
24 service crashes and four consecutive SMP-8 passes. Logs are in ignored
`build/d4-terminal-matrix.log` and `build/d4-terminal-{1..6}.uart.log`.
Neva passed **34 host tests** and **94/97/101** normal QEMU checks; Silt's
**three host/reference checks** pass. Neva's
`docs/architecture/d4-background-terminal-policy.md` records the contract,
artifact hashes and reproduction commands.

This closes the bounded UART background-mutation item, not D4. Next: broader
job/trap conformance and interrupted-operation resource cleanup. Orphaned-group
EIO rules, controlling-terminal acquisition and full POSIX PTY session semantics
also remain open. Earlier unattributed timeout/provider failures remain recorded
above. No hardware or full POSIX qualification is claimed; changes remain
uncommitted.

Fault-status follow-up, 2026-09-12: direct synchronous CPU faults now use Neva's
existing locked signal-return path, preserving typed termination just like
deferred pager failures. Invalid memory references report SIGSEGV, undefined
instructions SIGILL, and supported alignment/external/pager failures SIGBUS.
Blocked, ignored and nested faults terminate. Caught handlers can exit or
escape; normal exit 139 remains distinct from a signal death. Neva's legacy
single-instruction skip on ordinary handler return is explicitly nonportable.

The new check-faults fixture covers 42 fault/disposition cases and four normal
exit controls. Eight interactive dash cases check diagnostics, background wait,
pipeline last-member status and EXIT traps. The new fixture rejected the previous
kernel's exit-1 result before the fix. Neva's
`docs/architecture/d4-fault-signal-status.md` records the acceptance evidence and
precise runtime-versus-native coverage.

Final acceptance passed **106/106 checks at 1/4/8/8/8/8 vCPUs**, including four
consecutive SMP-8 runs and 24 service crashes (`build/d4-fault-matrix.log` and
`build/d4-fault-{1..6}.uart.log`). Neva passed **34 host tests**, **94/97/101**
normal checks and **89/92/96** VM-failure checks; armed fault recovery and EL1
panic gates also pass. Silt's **three host/reference checks** pass. Artifact
hashes and reproduction commands are in the Neva evidence document above.

This closes the bounded synchronous-fault reporting item, not all job/trap
conformance or D4. Next: interrupted-operation resource cleanup, particularly
temporary handles abandoned by a signal-handler longjmp. Broader upstream job
tests and the remaining orphan/controlling-terminal policies remain open.

Interruption-cleanup follow-up, 2026-09-12: Silt now checkpoints a temporary
handle cleanup stack in setjmp and unwinds it before longjmp discards frames.
Signals are masked through acquisition/adoption, closing the gap where a
handler could run before ownership was registered. Pipe/TTY readiness Events,
terminal/group operations and foreground transfer duplicates use this mechanism.
Normal completion closes the same records; CLOEXEC handles successful exec,
while failed exec and fork isolation preserve the expected ownership.

The A64 jmp_buf remains 176 bytes but its last word now stores the checkpoint;
all static Silt images were rebuilt. The new check-cleanup fixture checks exact
free-handle capacity across repeated EINTR and longjmp, nested-handler jumps,
acquisition-time delivery, exhaustion, fork and exec. The runner additionally
drives 160 consecutive dash prompt interrupts. Neva's
`docs/architecture/d4-interruption-cleanup.md` records coverage and evidence.

Stress testing also fixed two Neva prerequisites: valid signal handlers may live
on nonresident RX image pages, and a caught signal must not wake an IPC caller
before the transaction supplies its reply. IPC delivery stays pending until
completion; Event waits remain interruptible. A delayed-reply regression checks
the exact reply and caught delivery. This does not add cancellable IPC.

The UART harness now rejects truncated/overlong nsh commands, excludes echoes
from acceptance markers, aborts on timeout and rejects explicit boot self-test
failures. Its previously overlong three-stage pipeline case is shortened and
actually executed; earlier passing counts did not qualify that case.

Final exact-artifact matrix: **110/110 rootfs checks on 1/4/8/8/8/8 CPUs**,
including four consecutive eight-CPU runs and 24 injected service crashes.
Neva's 34 native host tests and Silt's four host/reference gates pass, as do
Neva's normal 94/97/101 and allocation-failure 89/92/96 CPU matrices. The linked
Neva evidence records hashes, commands, separate UART logs and failed attempts.

This is the bounded pipe/terminal temporary-handle slice, not complete libc
async-signal safety. An extra one-CPU pre-final-candidate run passed cleanup but
timed out later at `check-terminal show-erase` after foreground continuation.
That full-suite failure is retained and unattributed. Next: diagnose this
interactive timeout before broader upstream-compatible job/trap conformance.
Uncovered libc construction paths and orphan/controlling-terminal policies
remain open, along with the historical unattributed failures. D4 remains open.

Terminal-observer follow-up, 2026-09-12: the retained foreground/termios timeout
reproduced at cycle 81/128 on one CPU. A read-only snapshot found dash blocked
on a terminal Event, not child wait or IPC. Source inspection found sessiond
consuming the same counting readiness Event while polling for service restart.
Neva now provides NONBLOCK|PEEK observation, and sessiond uses it so lifecycle
polling cannot take a reader's notification. Ordinary consuming waits retain
their behavior; this is not a general PTY broadcast implementation.

Native tests and `check-terminal peek` cover repeated observation, later
consumption, empty state, flags and authority. The rootfs harness now supports
bounded repeated terminal cycles and optional read-only timeout snapshots with
exact retained kernel/ttyd symbols and terminal queue metadata. Per-command
deadlines and echo-exclusion rules are unchanged.

Neva's `docs/architecture/d4-terminal-observer-wakeup.md` records exact artifacts,
completed matrices and the evidence boundary. The initial failing snapshot lacked
queue contents; two 128-cycle negative-control runs with the consuming manager
passed. The source defect is fixed, but attribution of that exact historical hang
remains an inference, not a controlled failing-before/passing-after proof.
The fixed-build stress matrix passed 267/267 checks at 1/4/8/8/8 CPUs (32 terminal
cycles each), but the fourth consecutive SMP-8 attempt failed earlier in
`check-pipes` with code 10 and an instruction abort at the `fork` epilogue
(`0x1502c`). That is not a clean six-run acceptance result. The linked evidence
retains the exact log, snapshot and artifact identities.

SMP investigation then reproduced the same class of failure deterministically:
six children released onto one untouched executable page pass on one CPU but
die with SIGBUS on eight CPUs. Fault-time captures show one CPU observing the
matching pager request in PREPARING state while another creates its FrameGrant;
the VM/trap boundary promotes the resulting BUSY status to a synchronous fault.
Neva's `docs/architecture/d4-smp-investigation.md` records the exact evidence and
the separate ASID-rollover concern. The original `check-pipes` run did not retain
fault-time pager status, so its exact attribution remains open. Next: repair and
regress the transient pager race, audit ASID lifetime, and obtain a clean
consecutive matrix before broader job/trap coverage. D4 and full POSIX acceptance
remain open.

SMP retry follow-up, 2026-09-13: Neva now revalidates the mapping and yields on
transient pager preparation contention or completion before waiter attachment.
Silt includes `check-pager-smp`, releasing six children onto one cold executable
page. The independent frozen-artifact matrix passed **269/269 on 1/4/8/8/8/8
CPUs**, including four consecutive SMP-8 passes, 192 terminal cycles and 24
service crashes. Neva's normal matrix and both host suites also passed. See
Neva `docs/architecture/d4-smp-retest-2026-09-13.md` for exact identities and
the pending fixture-rebuild boundary; these results do not retroactively qualify
other artifacts. The baseline is approved for commit. Next: repair ASID lifetime
safety before broader upstream-compatible job/trap coverage. D4 remains open.

Commit preparation rebuilt the pending fixture and rootfs from the current Silt
sources. All four host checks and a fresh SMP-8 run with 32 terminal cycles and
four service crashes passed (269/269); logs are `build/d4-commit-refresh.log` and
`build/d4-commit-refresh.uart.log`. This supplements the frozen six-run matrix
without relabeling its artifact identity.

## Milestone D5: `/bin/sh` release

- Run upstream dash tests on Linux as the reference and under Neva/QEMU as the
  target, recording deliberate Silt deviations.
- Add script shebang handling and system startup scripts.
- Install dash as `/bin/dash` and `/bin/sh`; retain `/bin/nsh` in the recovery
  image and init-death path.
- Freeze the supported POSIX profile and publish a conformance matrix. Shell
  compatibility alone must not be described as full OS POSIX certification.

## Initial missing-surface ledger

| Surface | Neva/Silt baseline | Port action |
|---|---|---|
| `argc`/`argv` | bounded flat command text in current `nsh` exec path | structured process image ABI |
| `environ` | absent | startup environment block and libc ownership |
| descriptors | TCC-local compatibility table only | process-wide Silt descriptor contract |
| `pipe`/`dup2` | absent | capability-backed endpoints and descriptor operations |
| `fork`/wait | capability primitives exist | POSIX adapters with retained selectors |
| `execve` | executable VMO path exists | namespace resolve, argv/env, descriptor commit |
| cwd/PATH | shell-local `/bin/` construction | namespace-relative libc resolution |
| signals | core delivery exists | masks, interruption, `sigaction` semantics |
| process groups | capability-backed single-job baseline exists | secure multi-process pipeline adapter |
| termios | ttyd policy exists | POSIX terminal facade |
| glob/dirent | remote enumeration exists below libc | libc directory and pathname expansion support |
