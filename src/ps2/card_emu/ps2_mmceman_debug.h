#pragma once

#include <sys/time.h>
#include <stdio.h>
 
#define MMCEMAN_LOG_LEVEL 3
#define MMCEMAN_PROFILING 1

extern const char *log_level_str[];

#if MMCEMAN_LOG_LEVEL == 0
#define log_error(x...)
#define log_warn(x...)
#define log_info(x...)
#define log_trace(x...)
#else
#define MMCE_LOG(core, level, fmt, x...) \
    do { \
        if (level <= MMCEMAN_LOG_LEVEL) { \
            printf("%s C%i: "fmt, log_level_str[level], core, ##x); \
        } \
    } while (0);

#define log_error(core, fmt, x...) MMCE_LOG(core, 1, fmt, ##x)
#define log_warn(core, fmt, x...) MMCE_LOG(core, 2, fmt, ##x)
#define log_info(core, fmt, x...) MMCE_LOG(core, 3, fmt, ##x)
#define log_trace(core, fmt, x...) MMCE_LOG(core, 4, fmt, ##x)
#endif

#if MMCEMAN_PROFILING == 0
#define MP_CMD_START(x...)
#define MP_CMD_END(x...)
#define MP_SIGNAL_OP(x...)
#define MP_OP_START(x...)
#define MP_OP_END(x...)
#else
extern struct timeval tv_start_cmd, tv_end_cmd, tv_signal_mmce_fs, tv_start_mmce_fs, tv_end_mmce_fs;
extern void mmce_profiling_stat();
#define MP_CMD_START() gettimeofday(&tv_start_cmd, 0)
#define MP_SIGNAL_OP() gettimeofday(&tv_signal_mmce_fs, 0)
#define MP_OP_START() gettimeofday(&tv_start_mmce_fs, 0)
#define MP_OP_END() gettimeofday(&tv_end_mmce_fs, 0)
#define MP_CMD_END() do { \
                        gettimeofday(&tv_end_cmd, 0); \
                        mmce_profiling_stat(); \
                    } while (0)
#endif




