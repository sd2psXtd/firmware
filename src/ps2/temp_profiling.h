#ifndef TEMP_PROFILING_H
#define TEMP_PROFILING_H

#include <sys/time.h>

#define DEBUG_PROFILING

//TEMP: profiling stuff
extern struct timeval tv_start_cmd, tv_end_cmd, tv_signal_mmce_fs, tv_start_mmce_fs, tv_end_mmce_fs;

extern void debug_profiling_stat();

#ifdef DEBUG_PROFILING
#define DSTART_CMD() gettimeofday(&tv_start_cmd, 0)
#define DEND_CMD() gettimeofday(&tv_end_cmd, 0)
#define DSIGNAL_MMCE_FS_RUN() gettimeofday(&tv_signal_mmce_fs, 0)
#define DSTART_MMCE_FS_RUN() gettimeofday(&tv_start_mmce_fs, 0)
#define DEND_MMCE_FS_RUN() gettimeofday(&tv_end_mmce_fs, 0)
#define DSTAT() debug_profiling_stat()
#else
#define DSTART_CMD(x...)
#define DEND_CMD(x...)
#define DSIGNAL_MMCE_FS_RUN(x...)
#define DSTART_MMCE_FS_RUN(x...)
#define DEND_MMCE_FS_RUN(x...)
#define DTAT(x...)
#endif

#endif