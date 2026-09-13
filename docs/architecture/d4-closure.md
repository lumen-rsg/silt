# D4 session and PTY closure

Status: D4 closed on 2026-09-13, including session creation, controlling terminals and PTY lifecycle.

The 2026-09-13 request explicitly includes POSIX session creation,
controlling-terminal acquisition and PTY semantics. Those behaviors are now
implemented and exercised; they have not been moved to a later milestone.
D4 remains a Dash-port milestone, not certification of all POSIX libc interfaces,
serial hardware options or physical ARM SMP.

## Closure requirements

| Area | Implemented behavior and acceptance |
|---|---|
| Sessions/groups | Real setsid/getsid, setpgid/getpgid, inherited membership, parent/child and session-leader restrictions, failed versus committed exec, credential-checked parent/sibling queries; native assertions and kernel transactional host tests |
| Controlling terminal | Eligible-leader acquisition, O_NOCTTY, nonleader refusal, one association per terminal/session, inherited descriptors, dynamic /dev/tty, tcgetpgrp/tcgetsid, same-session foreground selection, retired-group/cross-session refusal, full-descriptor-table refusal before acquisition |
| PTY lifecycle | posix_openpt/grantpt/unlockpt/ptsname; slave open and stat metadata; shared NONBLOCK and dup/fork/exec/CLOEXEC ownership; queued input then EOF and write EIO on master loss; HUP/CONT of a stopped foreground job; slave reopen, stale names, leader exit, reacquisition, bounded pool exhaustion/recovery |
| Job-control policy | SIGTTIN/SIGTTOU, ignored/blocked exceptions, orphaned-group EIO, stopped orphan HUP/CONT, unconditional SIGSTOP, discarded default orphan TSTP/TTIN/TTOU; mandatory continuation with all eight ordinary child-report slots full |
| Terminal I/O | Canonical records and ordered empty/partial EOF; all four MIN/TIME cases; shared NONBLOCK; partial EINTR; drain/flush and all three tcsetattr actions; CR/NL/strip flags and explicit echo controls |
| Signals/errors | Signal-zero existence/permission checks, child-to-parent external signal delivery, WCONTINUED reporting, descriptor/PTY-pool refusal and recovery, preserved earlier Dash allocation/admission/interruption gates |
| Reference behavior | Existing twenty shared Linux/Silt job/trap cases including two attributable upstream-fix regressions, plus the same native MIN/TIME/EOF/flush/interruption/control assertions compiled against Linux libc; actual Silt Dash on a native PTY |
| Stability | Required host, focused, kernel 1/4/8/repeat-8 and fresh-media Silt 1/4/8/8/8/8 matrix on unchanged artifacts; all required runs pass, including six admission-refusal boots |

The native fixture source is `apps/check-sessions.c`, linked into the existing
`check-signals` image. Run `check-signals sessions` and
`check-signals sessions pty-dash`. A session test executes this same image again
to validate environment, controlling-terminal and descriptor inheritance.
The host-only build entry runs the portable terminal mode assertions.

## Contracts

Session/group authority comes from retained kernel Process/ProcessGroup
capabilities. Ttyd owns the controlling-terminal association and verifies caller
membership and opaque session identity. Sessiond supplies an attenuated client
controller. The shell's foreground-launch hooks resolve the current controlling
terminal instead of retaining the startup UART as permanent authority.

PTY names identify a provider instance plus a never-reused pair identity.
Grant selects the real UID, mode 0620 and group 0; unlocking is separate.
A query/stat does not open the slave or acquire a controlling terminal.
RemoteObject references, including pending calls, determine endpoint lifetime;
there is no POSIX heartbeat timeout. Dup and fork share status flags, and exec
preserves only descriptions with surviving descriptors. A retained terminal fd
becomes ordinary I/O after setsid, while /dev/tty follows the new association.

The kernel-side record is
`../neva-microkernel/docs/architecture/d4-session-pty-closure.md`; it documents
lock order, publication rollback, remote lifetime and orphan-report reservation.
The GIC regression is recorded in
`../neva-microkernel/docs/architecture/d4-gic-distributor.md`.

The [POSIX general terminal interface](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap11.html)
provides the canonical/noncanonical and session-terminal reference. The shared
Linux test explicitly accommodates asynchronous Linux input transfer before
flushing; it does not mistake immediate master-write completion for queued slave
input. Silt chooses EAGAIN and NONBLOCK precedence over MIN/TIME. Existing Dash
upstream attribution and its wait-all discrepancy remain in
[d4-job-trap-conformance.md](d4-job-trap-conformance.md).

## Failures retained during implementation

- `build/d4-pty-dash3.*`: Dash's PATH pointer referenced the DONTFORK exec handoff
  page. The child faulted in padvance_magic at FAR 0x07d8159d. Environment restore
  now validates then copies strings into inherited libc storage; a direct
  exec-then-fork environment assertion and the actual Dash PTY case pass.
- `build/d4-pty-lifecycle.*`: the new XSI-feature-macro caller's O_CLOEXEC value
  differed from the fallback used by libc. The fd survived exec. O_CLOEXEC and
  O_DIRECTORY now use the pinned header's ABI constants, with runtime CLOEXEC
  validation before and after exec.
- `build/d4-pty-dash.*` and `build/d4-orphan-native8.*`: filesystem provider boot
  failures led to the GIC shared-register race diagnosis. The old adapter loses
  updates in the native A/B control; the corrected adapter preserves them.
  The failed guest's exact distributor registers were not captured, so attribution
  does not extend to every historical provider failure without qualification.
- `../neva-microkernel/build-meson/d4-full-closure-20260913/rootfs-1-smp1.*`:
  a standalone seventeenth executable hit fsd's existing sixteen-binding limit
  and prevented the later check-signals launch. This matches the independently
  established boundary in the earlier ASID record. The complete session fixture
  now shares check-signals, preserving its workload without increasing filesystem
  quotas. This failed matrix is not counted as final acceptance.

## Explicit boundaries

The supported termios subset has ICANON, ECHO, ISIG, TOSTOP, NOFLSH, ECHOE,
ECHOK and ECHONL; ICRNL, INLCR, IGNCR and ISTRIP; VERASE, VKILL, VEOF, VINTR,
VSUSP, VQUIT, VMIN and VTIME. Unsupported flags, extra control characters and
baud settings are rejected. Serial parity/modem controls, output processing,
software flow control, packet mode and Linux-specific ioctls are not advertised.
The PTY master is the byte transport; terminal attribute/job-control calls use
the slave. PTYs fail closed across ttyd replacement; persistent PTY reconstruction
is not claimed. Existing supervised UART foreground recovery is separately tested.

Limits remain eight PTY pairs, 256 input/512 output bytes, 255 editable bytes in
the extended personality, 32 Silt descriptors, existing process/group quotas and
16 distinct fsd executable bindings. These are resource limits, not promises of
unbounded admission. The broader POSIX backlog still includes APIs beyond D4's
terminal/job-control needs, such as general group-selecting waitpid, kill(-1),
siginfo/alternate stacks and complete serial termios. None is represented as
implemented by this closure record.

## Final artifact evidence

The corrected matrix is retained under
`../neva-microkernel/build-meson/d4-full-closure-20260913-v4/`.
`results.json` records commands, exit codes and durations; `sha256.json` records
kernel, ttyd, rootfs, Dash and fixture identities. Each QEMU run creates private
media, and the matrix checks artifact hashes before and after every run.
The accepted results and hashes are recorded below.

The v2 sequence was deliberately interrupted after its passing 1/4/8 Silt runs
for foreground-selection review corrections; it is preliminary evidence only.
The final fixture checks cross-session EPERM and directly supplies a retained
empty group to ttyd, requiring rejection without changing the foreground owner.

PTY slave open modes are attenuated in the provider method mask as well as the
libc descriptor. Native acceptance bypasses libc with direct READ/WRITE service
calls and requires ACCESS_DENIED through a write-only/read-only capability. The
pre-attenuation kernel is retained as a negative control. The v3 preliminary
sequence passed kernel 1/4/8/8 and Silt 1/4 before deliberate interruption for
this capability correction; only v4 is the final acceptance sequence.

## Accepted matrix, 2026-09-13

All 18 final runs passed on unchanged artifacts. Each full Silt run uses
fresh private media, 32 terminal cycles, 160 prompt interrupts and four induced
ttyd/sessiond crashes. Four consecutive full SMP-8 runs pass. The six independent
admission boots each recover all 26 selected handoff/resume refusals, including
already-resumed pipeline prefixes: 156 refusals and 72 prefix observations total.
The focused session/PTY SMP-8 gate also passes all ten aggregate assertions.

| Run | CPUs | Passed | Seconds |
|---|---:|---:|---:|
| neva-host | host | 37/37 | 0.47 |
| silt-host | host | 9/9 | 0.64 |
| kernel-1-smp1 | 1 | 94/94 | 3.08 |
| kernel-2-smp4 | 4 | 97/97 | 4.12 |
| kernel-3-smp8 | 8 | 101/101 | 9.07 |
| kernel-4-smp8 | 8 | 101/101 | 8.83 |
| rootfs-1-smp1 | 1 | 455/455 | 110.02 |
| rootfs-2-smp4 | 4 | 455/455 | 120.13 |
| rootfs-3-smp8 | 8 | 455/455 | 290.14 |
| rootfs-4-smp8 | 8 | 455/455 | 299.04 |
| rootfs-5-smp8 | 8 | 455/455 | 278.83 |
| rootfs-6-smp8 | 8 | 455/455 | 288.51 |
| admission-1-smp1 | 1 | 142/142 | 15.57 |
| admission-2-smp4 | 4 | 142/142 | 14.10 |
| admission-3-smp8 | 8 | 142/142 | 33.90 |
| admission-4-smp8 | 8 | 142/142 | 36.60 |
| admission-5-smp8 | 8 | 142/142 | 32.00 |
| admission-6-smp8 | 8 | 142/142 | 31.46 |

| Artifact | SHA-256 |
|---|---|
| neva.elf | `da7289a75da5eb1366c8279d97ccb9ae9266298b4161a9abd4fb3ab2aa59785f` |
| ttyd.elf | `9e08dd55cc977b516cb4e8041c0ebe2536cd566fd40e1b3719d0f519929a5053` |
| silt-rootfs.img | `58a7eeac9926e647e37f0ebda4c6d415814e800b6a3ea19f7074aa1da58595a5` |
| dash.elf | `d1a197c7af54b48a8dff775bfc2a4de3190b577767ee7a2eb01db7d984d0a1cf` |
| check-signals.elf | `dc1cf3a72a50b9e8a4b8a06328af63fcb2849c4b778f1ba580ccf772c700f307` |

[d4-closure-evidence.json](d4-closure-evidence.json) preserves the commands,
per-run counts, exit codes, durations, tool versions, artifact identities and
39 changed source hashes. The local v4 directory retains the full UART/GDB logs,
matching per-run kernel/ttyd symbols, source copies, native test output and
negative controls. Artifact hashes are checked before and after every run; the
final verifier also checks current source bytes, per-run symbols, injection
markers and resumed-prefix counts. No artifact or source drift was found.
Earlier failed or deliberately interrupted sequences (v1/v2/v3), and negative
controls, are excluded from this acceptance. This is QEMU runtime acceptance,
with the documented terminal subset; physical hardware is not qualified.

Reproduce from the matching Neva/Silt sources with both normal Meson builds and
host suites, then run Neva `tools/test_runner.py build-meson --skip-build` with
`--smp=1`, `4`, `8`, `8`. Run Silt `tools/test_rootfs.py` with
`--neva-source ../neva-microkernel --neva-build ../neva-microkernel/build-meson
--rootfs build/silt-rootfs.img --restart-tests --terminal-cycles 32
--prompt-interrupts 160` on `--smp 1`, `4`, `8`, `8`, `8`, `8`. Run
`tools/test_pipeline_admission.py` with the same Neva/rootfs paths and CPU
sequence, plus a distinct `--output` directory per run. Exact argv and working
directories are recorded in the JSON; v4 also retains `run-matrix.py` and
`finalize_evidence.py`.
