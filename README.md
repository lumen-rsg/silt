# Silt OS

Silt is the capability-oriented operating system built on the Neva
microkernel. Neva owns the kernel mechanism and frozen capability ABI; Silt
owns the EL0 runtime, POSIX compatibility personality, services, shells,
utilities, system policy, and filesystem image.

The first userland milestone is a port of dash as `/bin/sh`. The existing
Neva shell (`nsh`) remains the bounded recovery and diagnostics shell.

## Current milestone

The repository currently provides:

- a deterministic 8 MiB Silt root filesystem with release metadata, nine
  capability-native diagnostic utilities, and `/bin/dash`;
- an end-to-end QEMU gate that boots that exact image on Neva, drives `nsh`,
  exercises service-backed input and system information, and verifies teardown;
- a reproducible, hash-pinned dash 0.5.13.5 source preparation step;
- a freestanding ARM64 cross-build that compiles every dash translation unit,
  links a Silt-owned libc baseline, and rejects unresolved process symbols;
- an explicit Silt configuration that starts without line editing or job
  control while the underlying POSIX descriptor/process adapters are built;
- architecture and implementation contracts for growing Silt without adding
  ambient authority to Neva.

The current D1 image runs a curated built-in-only `dash -c` suite in QEMU,
including assignments, quoting, parameter expansion, arithmetic, and control
flow. Files, descriptors, external commands, and pipelines remain explicit
D2/D3 work in `docs/plans/dash-port.md`; dash is not yet installed as `/bin/sh`.

## Build Silt

Neva and Silt are expected to be sibling checkouts:

```text
CLionProjects/
  neva-microkernel/
  silt-os/
```

Configure and compile with the pinned Arm GNU toolchain already used by Neva:

```sh
python3 tools/setup_meson.py build
meson compile -C build
```

The root filesystem is written to `build/silt-rootfs.img`. Boot it through the
automated four-vCPU acceptance runner with:

```sh
meson compile -C build rootfs-qemu-test
```

For an interactive Silt session, use the launcher. It configures and compiles
both sibling projects, boots clean writable copies of every disk image, and
leaves the build artifacts untouched:

```sh
python3 tools/run_silt.py
```

Useful variants include `--smp 1`, `--gui`, `--debug`, and `--dry-run`. Once
both projects are built, `--skip-build` starts immediately. The equivalent
Meson shortcut is:

```sh
meson compile -C build run
```

`tools/setup_meson.py` downloads the official dash release tarball, verifies
its SHA-256 digest, generates dash's derived sources with the host compiler,
and configures the freestanding ARM64 build. Network access is only needed
when the pinned tarball is absent from `subprojects/packagecache/`.

## Project layout

```text
docs/          Architecture contracts and executable port plans
apps/          Capability-native bootstrap utilities
include/       Public Silt userspace headers
libc/          Silt libc and POSIX personality implementation
ports/         Third-party port configuration and patches
services/      Supervised EL0 operating-system services
shells/        Native and ported shells
rootfs/        Root filesystem manifest and static staged files
system/        System policy and future service manifests
tests/         Host, ABI, and QEMU integration tests
tools/         Reproducible setup and source-preparation helpers
vendor/        Ignored, reproducibly prepared third-party source trees
```

## Upstream source policy

Prepared source trees in `vendor/` are ignored build artifacts and are never
the source of record. Silt-specific changes belong in `ports/<project>/` as
configuration, adapters, or narrowly scoped patches that are reapplied to a
verified upstream archive. Every imported release must record its version,
upstream commit, source URL, checksum, and license.
