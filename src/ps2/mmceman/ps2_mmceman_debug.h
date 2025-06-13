#pragma once

#include <sys/time.h>
#include <stdio.h>
#include <pico.h>

#define MMCEMAN_PROFILING 0

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




