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

As of 2026-08-16, D0 through D2 filesystem acceptance are complete. A clean,
checksum-verified source preparation applies the ordered Silt patch set. The
Arm GNU cross build produces the source-readiness archive and links it with a
Silt-owned libc baseline into `dash.elf`; a separate audit rejects unresolved
symbols in the process image. The image is installed as `/bin/dash` for the
built-in and filesystem QEMU gate. Fork, exec, wait, and pipe entry points remain
explicit `ENOSYS` boundaries for D3.

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

## Milestone D4: signals and interactive job control

- Complete `sigaction`, signal masks, `sigsuspend`, and interrupted-wait rules.
- Implement `isatty`, the required `termios` subset, `tcgetpgrp`, and
  capability-authorized `tcsetpgrp` through sessiond/ttyd.
- Enable dash `JOBS=1`; validate foreground/background pipelines, `jobs`, `fg`,
  `bg`, `^C`, `^Z`, `SIGTTIN`, `SIGTTOU`, and terminal restoration.

Acceptance: upstream job tests and Neva's UART/PTY interaction gates pass,
including service restart and consecutive clean SMP-8 runs.

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
