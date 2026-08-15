#pragma once

#include <stdint.h>

#include "process_image.h"

uint32_t silt_resolve_executable(const char* path);
int silt_descriptors_fork_prepare(void);
void silt_descriptors_fork_rollback(void);
void silt_descriptors_fork_inheritance(uint8_t* readers, uint8_t* writers);
void silt_descriptors_fork_commit(uint8_t readers, uint8_t writers);
void silt_descriptors_fork_discard(uint8_t readers, uint8_t writers);
int silt_descriptors_fork_child(void);
void silt_descriptors_process_exit(void);
int silt_descriptors_exec_export(SiltExecInfoV1* info);
int silt_descriptors_exec_restore(const SiltExecInfoV1* info);
void silt_environment_exec_restore(const SiltExecInfoV1* info);
