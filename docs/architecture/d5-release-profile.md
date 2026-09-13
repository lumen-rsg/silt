# D5 supported shell profile

Status: **D5 closed on 2026-09-13**; see the [acceptance record](d5-release-acceptance.md).
This freezes the supported Dash-port profile. It is not OS-wide POSIX certification
or a physical ARM64 hardware qualification.

## Installed session and recovery

The deterministic 8 MiB image installs dash 0.5.13.5 as `/bin/dash` and `/bin/sh`
(two names for one executable inode), plus `/bin/nsh`. The initial Neva shell
retains the recovery console while it runs the image's root-owned, non-writable
`/boot/session` policy. The policy names `sh /boot/session.sh`. That script sources
`/etc/profile`, sets the session environment, and execs interactive `/bin/sh`.
PATH is `/bin`, HOME is `/var/lib/shell`, SHELL is `/bin/sh`, the locale is `C`,
the creation mask is 022, and the normal prompt is `silt$ `.

Initd continues to own service startup and supervision. An attenuated, one-shot
`boot.ready` event releases the initial session only after initd's deliberately
destructive restart checks finish. It is not a reusable login-session broadcast.
A missing session policy keeps the standard Neva diagnostic shell. Invalid policy,
missing executable, or a failing startup script diagnoses failure and returns to
that retained shell. Exiting Dash also returns to nsh; `sh /boot/session.sh`
starts the session again. Running `nsh` from Dash launches the installed recovery
shell without rerunning assertions specific to the initial session leader.

If initd dies, Neva launches the sealed recovery image. This contains a minimal
nsh console mode with `help`, `about`, `status`, and `echo`. It needs only the
bootstrap console and remains usable without ttyd, sessiond, or filesystem
providers. Full filesystem or process-management commands are unavailable in
that dead-service-graph mode.

## Exec and scripts

Shebang parsing belongs to Silt's EL0 `execve`, before Neva receives an executable
capability. The kernel does not resolve script names or choose interpreters.

- Static AArch64 ELF execution retains Neva's validated, atomic exec transaction.
- Argument vectors contain 1–16 NUL-terminated entries within 127 bytes total,
  including terminators. Empty arguments and embedded whitespace are preserved.
- A script header starts with `#!`, ends with LF within the first 128 bytes, and
  names an absolute interpreter. Leading spaces/tabs are accepted. An optional
  remainder, trimmed of trailing spaces/tabs, becomes one interpreter argument.
  Quotes in the header are not shell syntax.
- The rewritten vector is interpreter, optional argument, script path, then the
  original argv[1..]. Four script hops are supported; another returns ELOOP.
- Each script must have executable mode bits and provider execute authorization.
  Read permission alone does not authorize direct execution. Missing interpreter,
  denied execution, malformed header, and expanded argument overflow return
  ENOENT, EACCES, ENOEXEC, and E2BIG respectively. Invalid ELF also fails closed.
- Script reads use the queried change generation. The interpreter subsequently
  reopens the script path; atomic execution across concurrent pathname replacement
  and set-ID scripts are not supported. Scripts do not allocate ELF pager bindings.
- Silt exec handoff version 3 preserves environment, cwd pathname, umask, and
  surviving descriptor descriptions; FD_CLOEXEC descriptors close on commit.
  Failed exec preserves application descriptors and releases temporary capabilities.
  All Silt images must be rebuilt together; mixed version-2/version-3 images are
  not a supported upgrade path.

The handoff has 64 environment entries and a shared 2048-byte string budget
(environment, cwd and descriptor paths), 32 descriptors and 32 descriptions, within
one read-only kernel-published page. Environment strings are copied into inherited
libc storage. Cwd is a normalized namespace pathname (at most 255 bytes), not a
retained directory identity across rename. Query replies are validated by ABI
magic/version/size before regular-file metadata or a new cwd is accepted.

## Conformance matrix

| Surface | Supported behavior | Evidence / deliberate limits |
|---|---|---|
| Shell language | Arithmetic, parameter expansion, quoting including empty arguments, field splitting, patterns, functions, loops/case, here-documents, substitutions, exit traps | 20 identical D5 cases on pinned Linux Dash and Silt; earlier D4 language/job/trap cases retained |
| External execution | PATH lookup, ELF and shebang execution, argv/environment/cwd/mask handoff, descriptor survival and CLOEXEC, refusal cleanup | Native `check-signals d5-exec`; bounded argv/header/chain/string limits above |
| Startup/login | `/boot/session.sh`, `/etc/profile`, normal interactive `/bin/sh`, login profile, re-entry after exit | Actual-image D5 gate and three separately built failing-startup images |
| Recovery | Installed nsh, parent recovery console, console-only initd-death mode | Actual launch/command/exit assertions and specialized initd-death boot |
| Signals and jobs | Caught/blocked signals, suspension, interruption, traps, foreground/background pipelines, quota refusal and cleanup | D4 shared Linux/Silt cases and repeated admission fault injection remain release gates |
| Sessions and terminals | setsid/getsid, scoped setpgid/getpgid, controlling-terminal acquisition, PTY lifetime, canonical/raw input, MIN/TIME, EOF, job-control signals | [D4 closure](d4-closure.md) is the current detailed contract, including explicit termios limits |
| Files and pipes | Namespace-scoped lookup, regular-file metadata, directory enumeration, redirection, capability byte streams and teardown | Fixed provider/storage limits remain; this is not a general POSIX filesystem |
| Build/image | Pinned upstream source, zero unresolved required symbols, exact executable contents, shared sh/dash inode, declared modes, deterministic image reconstruction | Host symbol and image gates; kernel and userspace rebuilt from the same reviewed sources |

## Reference provenance

The pinned [upstream source tree](https://kernel.googlesource.com/pub/scm/utils/dash/dash/+/037bbdfd330017c368caf6242f977974123239b5/)
does not ship a standalone upstream test suite. Accordingly, D5 does not claim to
have run such a suite. `rootfs/boot/d5-conformance.sh` contains twenty original
portable cases, executed unchanged by that pinned Dash on Linux and Silt. Its
literal-backslash dollar-quote pattern case is derived from the attributable
[upstream CTLESC fix](https://kernel.googlesource.com/pub/scm/utils/dash/dash/+/8fcf8a786a47a336843f2437b3866d2d4e81e9dc).
The earlier upstream-derived job/trap regressions and wait-all discrepancy remain
recorded in [D4 job/trap conformance](d4-job-trap-conformance.md).

## Deliberate boundaries

The image is a small shell and diagnostic userland, without a complete POSIX
utility set, dynamic linking, full locale support, or a general writable Unix
filesystem. The session's private `/tmp` supports one bounded file and reports
mode 0600; the separate NevFS mutable overlay is also bounded. Libc applies umask
to create requests, but the private-tmp provider retains that fixed mode policy.
Directory stat modes are synthetic 0555 and file link counts currently report 1;
on-disk hard-link count and shared file identity are verified separately. Full
shared regular-file offsets across fork, retained cwd under rename, filesystem symlinks and broad metadata mutation
are not advertised. `/bin/sh` is a hard link, not a symlink emulation.

Existing quotas remain 16 distinct fsd executable bindings, 32 Silt descriptors,
eight PTY pairs, and bounded process/group/domain admission. The SMALL Dash build
has no command-line editing library. Its numeric `umask` output is `22` rather
than Linux Dash's `0022`; both denote octal 022. The D4 restrictions on unsupported
termios flags, baud/modem/flow controls, packet mode, ttyd replacement, broader
wait selectors, kill(-1), siginfo and alternate signal stacks remain in force.

## Reproduce

Build both sibling projects, then run their Meson host suites. The focused release
gate is `meson compile -C build-meson d5-qemu-test` in Silt. The full retained D4
gate uses `tools/test_rootfs.py --restart-tests --terminal-cycles 32
--prompt-interrupts 160`; the admission gate uses `tools/test_pipeline_admission.py`.
All take the explicit Neva source/build and Silt rootfs paths. Use fresh media on
1/4/8 CPUs and repeat the eight-CPU runs. Run `tools/test_d5_boot_failures.py` for
separate missing-program, script-exit and malformed-policy images. Neva's
`b2-init-death-test` requires commands to succeed in the sealed recovery console.
