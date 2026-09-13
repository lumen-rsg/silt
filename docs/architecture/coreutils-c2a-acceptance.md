# GNU coreutils C2a acceptance — 2026-09-13

## Scope and source provenance

GNU coreutils 9.11 `pwd`, `printenv` and `yes` are added to the C1 command set.
Ten command names now share `/bin/coreutils`'s inode (eleven links total).
Explicit `/bin/pwd` selects GNU pwd; unqualified `pwd` remains the Dash builtin.
The GNU source lock and release archive are unchanged. Patch 0005 changes only
includes; all three command algorithms are retained. Yes uses the ordinary
write loop with the upstream Linux splice path configured out. Eighteen selected
gnulib implementation files match the unpatched release byte-for-byte, including
the four new long-options, exitfail, xgetcwd and root-dev-ino helpers.

Silt remains based on Dash D5 commit `ce8bcd165ff71f66b7b5d88b014dc26a0471629f`;
Neva remains at `28831dc7c1265baf0a16099a6c48e02c475f981b`. Acceptance was recorded for the
working-tree candidate before the utilities work was committed. The evidence
JSON preserves that original test-time status and baseline commits. No Neva source, service authority, kernel
primitive or quota was changed. See the [profile](../../ports/coreutils/README.md)
and [plan](../plans/coreutils-port.md).

## Runtime fixes and boundaries

- GNU printenv selects failure status 2. The shared close-output handler now
  honors gnulib's exit_failure policy; existing commands keep default status 1.
  Version output gains va_list forwarding for unchanged GNU option parsing.
- getcwd rejects undersized allocating requests before malloc, avoiding a leak.
  Its copy/allocation helper is exercised with short buffers and allocation
  refusal. It still reports Silt's bounded cwd; rename/deletion survival and
  filesystem symlink support are not provided by this milestone.
- Parent-component normalization previously retained a separator: resolving
  `/boot/c1/../c1` could store `/boot//c1`. Libc now removes the separator along
  with the last component. Native and target checks cover parent traversal,
  root clamping, relative paths, length bounds and failed chdir preservation.
- The synthetic virtual root now has inode 1 on synthetic device 0, distinct
  from synthetic character devices' zero identity. This prevents GNU pwd -L
  from accepting PWD=/dev/null, /dev/tty or /dev/ptmx at the root directory.
- Silt DIR retains a directory capability, not a POSIX descriptor. The port's
  optional dirfd adapter reports ENOTSUP, selecting GNU pwd's existing
  chdir/stat path in its reconstruction fallback. That code is retained and
  compiled; target acceptance exercises normal xgetcwd and logical validation,
  not forced reconstruction after cwd loss or unreadable parents.
- Existing exec budgets remain: 16 arguments and 127 bytes total for argv.
  A large yes operand is explicitly tested as an exec refusal; no limits were
  enlarged. Yes's large-operand reuse optimization is unreachable with this
  argv budget and is not claimed as target-tested. General directory mutation
  and ls remain the next filesystem milestones.

## Exact artifacts and checks

The frozen candidate is retained in `build/coreutils-c2a-frozen/`. All final
runs used identical kernel, service, initial media, reference and Silt artifacts.
The [machine-readable evidence](coreutils-c2a-evidence.json) records source,
artifact and log hashes, command lines, sanitizer checks and per-run results.
C1's historical artifacts remain unchanged in `build/coreutils-c1-frozen/`.

- Rootfs SHA-256: `c96467177c788cbb23bbbba246dea37703654bd20cff782f6d11980879e3247c`.
- Coreutils ELF SHA-256: `9a759e11f6b3d58566ea9ec2deb4f9c4cd55c6ca44e3707dba29a8e58a75f716`.
- Clean freestanding ARM64 build and host/reference/image gates: **19/19 passed**.
- New cwd and path tests pass AddressSanitizer and UndefinedBehaviorSanitizer.
- C0 regression on the new image: **77/77 passed**.
- C1 regression on the new image: **213/213 passed**.
- Dash D5 release smoke: **34/34 passed**.
- Broader eight-CPU rootfs suite: **315/315 passed**, with four terminal cycles,
  160 prompt interrupts and four service crashes. This is not D5's entire
  historical 35-boot release matrix.

| Final directory under build/ | CPUs | Result |
|---|---:|---|
| coreutils-c2a-final-1-smp1 | 1 | 126/126 |
| coreutils-c2a-final-2-smp4 | 4 | 126/126 |
| coreutils-c2a-final-3-smp8 | 8 | 126/126 |
| coreutils-c2a-final-4-smp8 | 8 | 126/126 |

Runs are serialized with ten-second spacing; both eight-CPU passes are
consecutive. This matrix had no launch or guest failures. The earlier C1 host
io_uring allocation failures and C0 pre-session boot failure remain historical
observations; this result does not diagnose or erase them.

The 126 focused checks compare the pinned, unpatched GNU/Linux release and the
installed Silt commands. Pwd covers physical/logical selection, option precedence,
PWD validation and spellings at several accessible directories; only the Linux
reference root prefix is normalized. Printenv runs with an identical handcrafted
exec environment on Linux and Silt, including duplicate names, empty values,
newlines, unnamed and malformed entries, NUL output and missing variables.
Yes output is read in bounded 1/513/4097-byte prefixes from the real GNU producer;
closing the reference pipe must terminate it with SIGPIPE. The target checks
output digest/length and producer status, default/ignored SIGPIPE, output errors,
32 repeated early consumer exits and descriptor recovery. Help/version prefixes
and multicall dispatch are checked separately. Adapted diagnostic/help prose is
not asserted byte-identical; these are original tests, not the complete upstream
GNU test suite. No physical hardware qualification was performed.

## Retained development observations

The logs in `build/coreutils-c2a-focus*` are preliminary, not final acceptance.
The first target cwd check exposed the parent-separator bug. A later harness
attempt assumed `/usr/share/licenses/coreutils` was traversable in the session
namespace; it is staged in the image but that path is not exposed there. The
comparison now uses accessible directories without enlarging the namespace.
Another attempt tried a 600-byte yes operand and correctly hit the existing
exec argv budget. The final tests use an in-budget operand and retain an explicit
oversize-refusal check. Header integration initially found an unavailable
sys/uio.h (needed only for disabled splice) and a missing stdlib declaration in
the new test fixture; both were corrected before the clean final build.

## Reproduction

From Silt, with the sibling Neva SDK built:

```sh
python3 tools/setup_meson.py build
meson compile -C build
meson test -C build --print-errorlogs
python3 tools/prepare_coreutils_reference.py
meson compile -C build coreutils-inspection-qemu-test
python3 tools/test_coreutils_inspection.py --smp 8 --output build/c2a-smp8
```

Repeat at one/four/eight CPUs with distinct output directories. The evidence
JSON records the exact additional C0/C1/D5/rootfs regression commands. The
normal shell remains Dash; /bin/true remains Neva's pager/FP fixture.
