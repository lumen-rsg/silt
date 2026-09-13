#pragma once

#include <stdint.h>

#include "process_image.h"
#include "tty.h"
#include "neva_abi.h"
#include "cleanup.h"

uint32_t silt_resolve_executable(const char* path);
uint32_t silt_group_acquire_guarded(SiltCleanup* cleanup, int process_group);
NevaStatus silt_tty_set_foreground(uint32_t tty, uint32_t group);
NevaStatus silt_controlling_set_foreground(uint32_t group);
int silt_tty_read(uint32_t tty, void* buffer, size_t size, int flags);
int silt_tty_write(uint32_t tty, const void* buffer, size_t size, int flags);
uint32_t silt_pty_open(uint64_t identity, int flags, int master);
int silt_pty_metadata(uint64_t identity, NevaTtyMetadataV1* metadata);
int silt_pty_io(uint32_t master, void* buffer, size_t size, int flags, int writing);
uint32_t silt_descriptor_tty(int descriptor);
int silt_tty_is_controlling(uint32_t tty);
void silt_descriptors_process_exit(void);
int silt_descriptors_exec_export(SiltExecInfoV2* info);
int silt_descriptors_exec_restore(const SiltExecInfoV2* info);
void silt_environment_exec_restore(const SiltExecInfoV2* info);
