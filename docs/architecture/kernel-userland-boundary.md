# Neva and Silt boundary

## Purpose

Neva is a capability microkernel. Silt is the operating-system personality
that translates conventional application APIs into explicit Neva object and
service operations. Porting POSIX software must preserve that separation.

## Ownership

Neva owns:

- scheduling, address spaces, exceptions, signals, and process lifecycle;
- typed kernel objects, capability handles, rights attenuation, and revocation;
- capability-backed fork, executable VMO replacement, wait events, and process
  group identities;
- the stable userspace ABI needed to invoke those mechanisms.

Silt owns:

- the C runtime, `errno`, environment, locale baseline, and POSIX headers;
- file descriptors as process-local names for capability-backed open objects;
- current directory and path resolution within the process namespace grant;
- pipes, redirection, descriptor duplication, and close-on-exec policy;
- POSIX PID/process-group adapters authorized through retained capabilities;
- terminal policy adapters, shells, utilities, services, and the system image.

## Non-negotiable authority rules

1. A numeric PID, process-group ID, file descriptor, or pathname is metadata,
   never ambient authority.
2. Silt adapters must resolve metadata through an authority already held by the
   calling process and must not introduce a global lookup escape hatch.
3. Descriptor inheritance attenuates or preserves explicit capabilities; it
   does not republish global kernel handles.
4. `execve()` resolves an executable through the caller's namespace and passes
   an executable VMO capability to Neva.
5. `tcsetpgrp()` is authorized by the session and retained ProcessGroup
   capability, not by trusting a caller-supplied integer.
6. Compatibility failures are reported explicitly. Silt must not silently
   approximate POSIX behavior when doing so would change authority or lifetime.

## ABI release discipline

The Neva userspace ABI will be exported as a versioned SDK. Silt may depend on
released ABI declarations and libraries but not on Neva core or adapter
internals. Until that SDK target exists, the sibling source path is accepted
only as a build-time transition aid.
