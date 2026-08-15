# Silt root filesystem R0

## Goal

Produce the first Silt-owned system image that boots on Neva, preserves `nsh`
as the recovery shell, and provides enough userland to inspect the running
system before the dash port becomes executable.

## Ownership boundary

Silt owns the root filesystem manifest, release metadata, utilities, and image
assembly. Neva owns the current NevFS on-disk format, the capability ABI, and
the EL0 filesystem/pager mechanisms used to mount and execute the image.

The sibling Neva build is a transitional SDK provider. Silt consumes its
startup object, userspace ABI archive, headers, linker script, and deterministic
NevFS formatter. This dependency must eventually become a versioned SDK rather
than an unversioned source-tree relationship.

## Image contract

`rootfs/manifest.json` is the source of truth for R0. It fixes:

- the `nevfs-v1` format and an 8 MiB image size;
- the sorted `/bin` application inventory;
- the static paths that must exist before formatting can succeed.

Image creation uses `SOURCE_DATE_EPOCH=0`, sorted application inputs, and the
Neva formatter's deterministic allocation order. Prepared third-party dash
sources do not enter this image yet.

R0 installs:

- `/bin/echo`, `/bin/false`, `/bin/id`, `/bin/meminfo`, `/bin/ps`;
- `/bin/sysinfo`, `/bin/true`, `/bin/uname`, `/bin/whoami`;
- `/etc/os-release` and `/etc/motd`;
- the deterministic bootstrap account files and recovery fixture supplied by
  the NevFS formatter.

The bootstrap identity map contains only `root` (UID 0) and `session` (UID
1000). Name-service-switch and mutable passwd lookup belong to the Silt libc
milestone; R0 utilities use this bounded map and do not acquire ambient VFS
authority.

## Execution and namespace rules

NevFS exposes packaged regular files from inode metadata rather than a
hard-coded executable name. Execute-map authority is granted only when the
inode has an execute mode bit. The supervised session receives explicit
read-only grants for `/bin`, `/boot`, `/etc/passwd`, `/etc/group`,
`/etc/os-release`, `/etc/motd`, and `/test.txt`; `/etc/shadow` remains absent
from the session namespace.

The current Neva pager acceptance invokes a fixed instruction probe in
`/bin/true`. Silt retains this transition fixture so replacing the system image
does not weaken Neva's executable-page test. It should move into a dedicated
test artifact once boot acceptance no longer depends on a conventional command.

## Acceptance

Build and boot the exact Silt image with:

```sh
python3 tools/setup_meson.py build
meson compile -C build
meson compile -C build rootfs-qemu-test
```

The QEMU gate runs at four vCPUs and requires:

- initd, named, filesystem providers, vfsd, and the user session to reach
  their readiness markers;
- canonical `nsh` editing and live input-service statistics;
- readable Silt release metadata and message of the day;
- successful execution of every R0 utility through NevFS PagerFile VMOs;
- capability-clean session teardown.

## Next boundary

R1 should move system unit policy and account seeding into Silt-owned inputs,
export a versioned Neva userspace SDK, add a rootfs inspection test independent
of QEMU, and install the first linked dash binary when D1 is complete.
