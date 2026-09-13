#pragma once

// Internal dash adapter hooks. They bracket one pipeline so fork() can create
// and attach every suspended stage to one capability-backed ProcessGroup.
// Finish/end return -1 after reclaiming construction on admission refusal;
// the caller must discard its job record and restore the terminal foreground.
#ifdef SILT_PIPELINE_PREPARE_HOST
static inline void silt_pipeline_begin(void) {}
static inline int silt_pipeline_end(void) { return 0; }
static inline void silt_pipeline_abort(void) {}
static inline void silt_job_prepare(int group, int foreground) { (void)group; (void)foreground; }
static inline int silt_job_finish(int pid) { (void)pid; return 0; }
#else
void silt_pipeline_begin(void);
int silt_pipeline_end(void);
void silt_pipeline_abort(void);
void silt_job_prepare(int group, int foreground);
int silt_job_finish(int pid);
#endif
