#pragma once

// Internal dash adapter hooks. They bracket one pipeline so fork() can create
// and attach every suspended stage to one capability-backed ProcessGroup.
#ifdef SILT_PIPELINE_PREPARE_HOST
static inline void silt_pipeline_begin(void) {}
static inline void silt_pipeline_end(void) {}
static inline void silt_pipeline_abort(void) {}
static inline void silt_job_prepare(int group, int foreground) { (void)group; (void)foreground; }
static inline void silt_job_finish(int pid) { (void)pid; }
#else
void silt_pipeline_begin(void);
void silt_pipeline_end(void);
void silt_pipeline_abort(void);
void silt_job_prepare(int group, int foreground);
void silt_job_finish(int pid);
#endif
