# GNU coreutils C1 acceptance — 2026-09-13

C1 is historical evidence. C2a now replaces the default build outputs; the
exact C1 artifacts remain in `build/coreutils-c1-frozen/` with the hashes below.

## Candidate and supported profile

This extends the C0 working tree after Silt Dash D5 baseline
`ce8bcd165ff71f66b7b5d88b014dc26a0471629f`. Neva remains at
`28831dc7c1265baf0a16099a6c48e02c475f981b`; no kernel/service source or quota
changes are part of C1. Acceptance was recorded before the utilities work was committed.

The new commands are GNU coreutils 9.11 `cat`, `head`, finite `tail` and
single-byte C-locale `wc`. They share `/bin/coreutils`'s inode with the three
C0 commands. `/bin/sh` remains Dash; `/bin/true` remains the dedicated pager/FP
fixture. Follow/retry/PID monitoring/sleep options in tail and `--files0-from`
in wc explicitly fail with status 1. `wc -m` counts single-byte characters;
other locale settings do not enable Unicode processing. See the
[command profile](../../ports/coreutils/README.md) and
[milestone plan](../plans/coreutils-port.md).

## Implementation and interface findings

- Cat and head retain their upstream command algorithms; their patch changes
  includes only. Tail excludes follow/monitoring code, wc excludes multibyte and
  filename-stream paths, and both have profile-specific help. Fourteen selected
  gnulib implementation files, including checked count parsing and safe/full
  I/O, match the unpatched release byte-for-byte.
- The port supplies checked allocation/alignment, conditional nonreturning
  diagnostics, counter formatting, EINTR-safe reads and C-locale classification.
  Unsupported Linux copy acceleration returns ENOSYS for GNU's ordinary copy
  fallback. Access advice and binary-mode selection have no transport effect;
  standard streams are unbuffered. This does not introduce general fopen/fread
  or multibyte stdio support.
- Pipe fstat now returns FIFO metadata locally instead of sending RemoteFile
  query methods to ByteStream handles. File and directory metadata preserve the
  service's 64-bit `volume_id` in st_dev and `object_id` in st_ino. Provider object
  IDs can overlap; st_dev is required to distinguish them. Private temporary
  volume/object IDs exceed 32 bits. Link counts remain the existing synthetic 1;
  this is not a general filesystem metadata completeness claim.
- Libc signed conversion now handles signs and signed limits correctly;
  unsigned overflow saturates even with a minus sign, and a bare 0x prefix stops
  at x. GNU count parsers use these conversions. Libc also provides memchr and
  stpcpy; Dash's required-symbol contract records its stpcpy dependency.

## Validation

The final frozen artifacts and all source/log hashes are recorded in
[the machine-readable evidence](coreutils-c1-evidence.json). The actual Silt
ELFs and rootfs are retained in `build/coreutils-c1-frozen/` and match the
ordinary build outputs byte-for-byte. C0's historical artifacts remain in
`build/coreutils-c0-frozen/`; the normal build now contains C1.

- Host/reference/image gates: **17/17 passed** (`build/coreutils-c1-host.log`).
- C0 command regression on this image: **77/77 passed**.
- Dash D5 release smoke: **34/34 passed**.
- Broader rootfs regression on eight CPUs: **315/315 passed**, with four terminal
  cycles, 160 prompt interrupts and four service crashes. This is not a rerun
  of D5's complete 35-boot release matrix.

| Final run under build/ | CPUs | Result |
|---|---:|---|
| coreutils-c1-final-1-smp1 | 1 | 213/213 |
| coreutils-c1-final-2-smp4 | 4 | 213/213 |
| coreutils-c1-final-3-smp8 | 8 | Host QEMU launch failure; no guest boot |
| coreutils-c1-final-4-smp8 | 8 | 213/213 |
| coreutils-c1-final-5-smp8 | 8 | Host QEMU launch failure; no guest boot |
| coreutils-c1-final-6-smp8 | 8 | 213/213 |
| coreutils-c1-final-7-smp8 | 8 | 213/213 |

The final two eight-CPU runs are consecutive and serialized, with a ten-second
pause between launches. Both preceding host launch failures are retained: QEMU
reported `Failed to initialize io_uring: Cannot allocate memory` before any
guest instruction or utility case ran. A
retry with unchanged settings and artifacts started normally. These successful
runs do not make the first-attempt matrix entirely clean. The pause is
operational spacing, not an established fix for the host allocation failure.

Final rootfs SHA-256: `4b034b23df2d2b40acf1eb5a495c75038b25ac4d1defb50c0ac14048ddb0bcb2`.
Final coreutils ELF SHA-256: `3ef3fe8d3f7e8a2fc0934d5d13c5a2b232ab2b90dbbb5762e0a9034d6485e066`.

The focused cases compare output byte length and FNV-1a digest with the
unpatched GNU 9.11 Linux binaries under LC_ALL=C, and check both producer and
consumer statuses. The digest avoids the console's NUL filtering. These are
original differential cases, not the entire GNU upstream test suite. Raw UARTs
and result JSONs retain each case and exact kernel/service/disk/reference hashes.

Coverage includes every byte value over multiple buffers, long/unterminated
lines, empty input, regular files and short pipe transfers, signed/overflow
counts and GNU suffixes, NUL delimiters, headers and totals, transformations
across file boundaries, explicit stdin operands, missing/directory/closed-output
errors, GNU NBSP/POSIXLY_CORRECT behavior and legacy POSIX-version parsing.
Target checks also verify same-file refusal without data loss, 32 repetitions
of early consumer exit, pipe type, hardlink identity, 64-bit temporary identities,
volume separation and descriptor exhaustion/recovery.

Native tests inject interrupted/short reads, EOF and hard read errors, plus the
retained stdio short-write/EINTR/no-progress/close/OOM tests and exit callbacks.
New integer tests cover signed/unsigned limits, overflow, invalid signs, prefix
and end-pointer behavior. Integer tests also pass with AddressSanitizer and
UndefinedBehaviorSanitizer. The clean freestanding build retains warnings as
errors with explicit upstream unused-parameter/sign/type-limit/unused-variable
allowances. No newlib reentrant stream runtime or unresolved symbols are linked.

## Retained development observations

1. Initial wc binary comparisons disagreed by one word per 256-byte block.
   The C-locale adapter omitted GNU's default 0xa0 NBSP separator. This was
   corrected and tested with and without POSIXLY_CORRECT. Logs are retained in
   `build/coreutils-c1-focus*`.
2. The first expanded metadata assertion incorrectly required the read-only
   provider's object ID to exceed 16 bits. That provider uses small IDs. The
   corrected assertion tests the real private temporary IDs above 32 bits and
   hardlink identity. See `build/coreutils-c1-expanded` and
   `build/coreutils-c1-metadata`.
3. A final-harness preflight rejected a 131-byte terminal command before sending
   it. The overflow input was shortened to remain an overflow within the
   terminal's 126-byte limit; the observation remains in
   `build/coreutils-c1-frame-preflight`.
4. A subsequent source audit found that st_dev was still zero, allowing IDs
   from distinct providers to alias. C1 now preserves volume IDs as well as
   object IDs, with target assertions for both. Earlier 1/4-CPU passes and an
   intentionally interrupted eight-CPU run are retained under
   `build/coreutils-c1-pre-volume-*`; their image is in the corresponding frozen
   directory. They are superseded by the complete final matrix, not counted as
   final acceptance. The focused corrected metadata run is
   `build/coreutils-c1-volume-metadata`.

No physical hardware qualification was performed. This closes the bounded C1
utility profile only. Full coreutils, locale support, complete stdio and the
C2 directory/mutation commands remain open. C0's earlier pre-session SMP-8 boot
failure remains a historical unattributed observation; the C1 passes do
not diagnose or erase it.

## Reproduction

From the Silt checkout, with the sibling Neva SDK built:

```sh
python3 tools/setup_meson.py build
meson compile -C build
meson test -C build --print-errorlogs
python3 tools/prepare_coreutils_reference.py
python3 tools/test_coreutils_streams.py --smp 1 --output build/c1-smp1
python3 tools/test_coreutils_streams.py --smp 4 --output build/c1-smp4
python3 tools/test_coreutils_streams.py --smp 8 --output build/c1-smp8-a
python3 tools/test_coreutils_streams.py --smp 8 --output build/c1-smp8-b
python3 tools/test_coreutils.py --reference build/coreutils-reference/build/src \
  --smp 4 --output build/c1-c0-regression
python3 tools/test_d5_release.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 4 --output build/c1-d5-regression
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --restart-tests --terminal-cycles 4 --prompt-interrupts 160 \
  --uart-log build/c1-rootfs-smp8.uart.log
```
