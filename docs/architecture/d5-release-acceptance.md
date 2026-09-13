# D5 release acceptance

Status: **closed on 2026-09-13** for the supported behavior frozen in
[d5-release-profile.md](d5-release-profile.md). All 33 final gates passed, covering
35 fresh QEMU boots, without frozen source or boot-artifact drift.

## Scope and evidence policy

D5 makes Dash the normal `/bin/sh`, adds Silt-owned shebang exec and startup policy,
and retains both ordinary and initd-death nsh recovery. D4's expanded session,
controlling-terminal and PTY requirements remain release gates. Acceptance is
QEMU runtime evidence, not physical ARM64 or OS-wide POSIX certification.

The final driver is retained at
`../neva-microkernel/build-meson/d5-release-20260913-v3/run_matrix.py`.
It stops on failure and checks the frozen source and artifact hashes before and
after every run. It retains commands, exit codes, elapsed times, UART and injection
logs, matching kernel/ttyd symbols, executable/media copies and a source archive.
`verify.py` independently verifies counts, hashes, per-run symbols and actual
injection/resumed-prefix markers before producing the durable JSON record.

Completed gates:

- Neva and Silt host suites, including the Linux reference cases, symbol closure,
  deterministic image rebuild and COW copyout control.
- Canonical Neva kernel on 1/4/8/8 CPUs, and VM allocation rollback on 1/4/8.
- Interactive sealed initd-death recovery on 1/4/8/8 CPUs.
- Normal D5 release on 1/4/8/8/8/8 CPUs: startup, login, native exec assertions,
  shared shell cases, installed nsh, normal exit and session re-entry.
- Complete Silt/D4 rootfs on 1/4/8/8/8/8 CPUs, each with 32 terminal cycles,
  160 prompt interrupts and four induced ttyd/sessiond crashes.
- Admission refusal on 1/4/8/8/8/8 CPUs, each with 26 injected refusals and
  12 observations of an already-resumed pipeline prefix.
- Two eight-CPU sets of three separately built failing-startup images: missing
  program, script exit 42 and malformed multiline policy; each must diagnose the
  failure and accept `about` at the retained nsh prompt.

## Development failures excluded from acceptance

- Initial boot probes ran the session before initd's deliberate provider/TTY
  restarts. The script read failed and nsh recovered. An explicit `boot.ready`
  rendezvous now orders normal startup after the restart checks. An early attempt
  to use blocking EVENT_WAIT_PEEK was invalid; the final one-shot event is consumed.
- Native exec tests exposed dropped cwd/umask, empty-argument rejection and a
  regular-file query mistaken for a directory because both methods used number 1.
  Handoff version 3, empty-vector-entry support and typed query validation fix
  those cases. The test also requires a failed chdir to preserve cwd.
- Direct execution of a mode-0644 script originally succeeded because namespace
  resolution could return attenuated read/metadata authority. Silt now verifies
  execute authorization and executable mode bits before accepting a shebang.
- Early test fixtures attempted multiple mutable files and assumed arbitrary
  modes in private `/tmp`. Its existing single-file/mode-0600 policy is retained;
  refusal scripts are immutable, explicitly mode-marked release fixtures. Those
  fixture failures are not claims of generalized writable-filesystem support.
- The formatter's new hard-link count initially used an incorrect inode offset;
  image inspection caught it. The correct offset is 34. Exact-size review also
  found and removed an older slice assignment that shortened every image by four
  bytes. The image gate now verifies full declared length and byte reconstruction.
- Installed nsh initially ran the initial-leader/BootBundle self-checks and
  rejected a normal Dash child. Those boot-specific checks now run only for the
  initial session; ordinary terminal/group/namespace initialization remains.
- Two eight-CPU failure-policy boots failed the initial C7 fork/exec membership
  query before the policy was read, then lost the shell and terminal recovery.
  The [Neva COW copyout record](../../../neva-microkernel/docs/architecture/d5-cow-copyout.md)
  documents the discovered transient-claim bug and old/fixed host control. The
  precise historical guest claim owner was not captured, so that attribution
  remains an inference. Earlier logs and read-only snapshots are retained.
- The v1 acceptance sequence was deliberately stopped after review corrected a
  failed-query diagnostic that could read uninitialized metadata. Its passing
  short gates are preliminary, not the final matrix.
- V2 passed the short gates and all six D5 release boots, then the full one-CPU
  rootfs suite could not launch `check-pipes`. Fsd keyed executable bindings by
  directory-entry node, so on-disk sh/dash hard links consumed two cache slots.
  It now canonicalizes file and pager identity by disk inode. Native acceptance
  checks equal sh/dash identity and repeated execution through both names; the
  full 455-check one-CPU suite then passed without increasing the cache quota.

## Final results

The [durable evidence record](d5-release-evidence.json) records all 33 commands,
exit codes, durations and counts, 563 source hashes, 12 artifact hashes and 213
log hashes. Verification also matched retained kernel/ttyd symbols and the actual
fault-injection markers. Earlier failures and interrupted sequences above remain
excluded from these totals.

| Gate | CPU sequence | Passing checks per run |
|---|---|---|
| Neva host | native | 38 |
| Silt host and Linux references | native | 11 |
| Canonical kernel | 1 / 4 / 8 / 8 | 94 / 97 / 101 / 101 |
| VM allocation rollback | 1 / 4 / 8 | 89 / 92 / 96 |
| Interactive initd-death rescue | 1 / 4 / 8 / 8 | status and echo succeeded in all four boots |
| D5 release | 1 / 4 / 8 / 8 / 8 / 8 | 34 each |
| Full D4 rootfs | 1 / 4 / 8 / 8 / 8 / 8 | 455 each |
| Pipeline admission refusals | 1 / 4 / 8 / 8 / 8 / 8 | 142 each |
| Failed-startup recovery | two sets of three on 8 | 6 per image, 18 per set |

The four consecutive full SMP-8 rootfs runs took 301.10, 282.05, 257.54 and
250.21 seconds. Each retained 32 terminal cycles, 160 prompt interrupts and four
induced service crashes (two each of ttyd and sessiond). The six admission boots observed all
156 injected refusals and 72 already-resumed pipeline prefixes. All six distinct
failed-startup boots diagnosed failure and accepted commands at the recovery
prompt. The old COW copyout control failed its valid-copy assertion (exit -6);
the corrected control passed (exit 0).

### Accepted artifact identities

SHA-256 values below identify the actual QEMU artifacts; the JSON includes all
12 artifacts and tool versions. Neither document claims physical hardware
qualification or unrestricted POSIX conformance.

| Artifact | SHA-256 |
|---|---|

| `neva.elf` | `7043aa8b67f653110e6278da2f60b18ff6bfbdc95a3380148f465758585cf78b` |
| `ttyd.elf` | `9e08dd55cc977b516cb4e8041c0ebe2536cd566fd40e1b3719d0f519929a5053` |
| `silt-rootfs.img` | `a441d332b3e5385d6b841e0586260492155c7f1ac52dec8c498e122b41fcdeaa` |
| `dash.elf` | `5b40c5d186298b67fbabf5c7d99c9b1e69ca794669f02cbafd8e0832c4cb7a2d` |
| `check-signals.elf` | `32cf5b2a3ec88f0804c118fd34d70bb45c2d6f4306c0a30fd071abe231209a9a` |

The source archive and per-source hashes cover the reviewed changes above the
recorded D4 base commits; documentation is deliberately outside the runtime
freeze. Required sibling Neva commits are `5d7d545` (COW copyout) and `28831dc`
(startup, recovery and shared executable identity). The local retained run is
`/home/cv2/CLionProjects/neva-microkernel/build-meson/d5-release-20260913-v3`.
