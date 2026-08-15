# Silt OS

Silt is the capability-oriented operating system built on the Neva
microkernel. Neva owns the kernel mechanism and frozen capability ABI; Silt
owns the EL0 runtime, POSIX compatibility personality, services, shells,
utilities, system policy, and filesystem image.

The first userland milestone is a port of dash as `/bin/sh`. The existing
Neva shell (`nsh`) remains the bounded recovery and diagnostics shell.

## Current milestone

The repository currently provides:

- a reproducible, hash-pinned dash 0.5.13.5 source preparation step;
- a freestanding ARM64 cross-build that compiles every dash translation unit
  into a static port archive;
- an explicit Silt configuration that starts without line editing or job
  control while the underlying POSIX descriptor/process adapters are built;
- architecture and implementation contracts for growing Silt without adding
  ambient authority to Neva.

The archive is a compile-readiness gate, not yet a runnable shell. Linking and
booting dash require the libc and descriptor work recorded in
`docs/plans/dash-port.md`.

## Build the port gate

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

`tools/setup_meson.py` downloads the official dash release tarball, verifies
its SHA-256 digest, generates dash's derived sources with the host compiler,
and configures the freestanding ARM64 build. Network access is only needed
when the pinned tarball is absent from `subprojects/packagecache/`.

## Project layout

```text
docs/          Architecture contracts and executable port plans
include/       Public Silt userspace headers
libc/          Silt libc and POSIX personality implementation
ports/         Third-party port configuration and patches
services/      Supervised EL0 operating-system services
shells/        Native and ported shells
system/        System manifests and filesystem-image assembly
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
