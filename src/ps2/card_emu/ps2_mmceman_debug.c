#include "card_emu/ps2_mmceman_debug.h"

#include <stdio.h>

struct timeval tv_start_cmd, tv_end_cmd, tv_signal_mmce_fs, tv_start_mmce_fs, tv_end_mmce_fs;

const char *log_level_str[] = {
    " ",
    "[ERROR]",
    "[WARN]",
    "[INFO]",
    "[TRACE]"
};

void mmce_log(int core, int level, const char *fmt, ...)
{
    if (level <= MMCEMAN_LOG_LEVEL)
        printf("C%i %s: %s", core, log_level_str[level], fmt);
}

//TODO: fix
void mmce_profiling_stat()
{
    long cmd_elasped = (tv_end_cmd.tv_sec - tv_start_cmd.tv_sec) * 1000000 + tv_end_cmd.tv_usec - tv_start_cmd.tv_usec; 
    //long mmce_fs_elasped = (tv_end_mmce_fs.tv_sec - tv_start_mmce_fs.tv_sec) * 1000000 + tv_end_mmce_fs.tv_usec - tv_start_mmce_fs.tv_usec; 
    //long cmd_mmce_fs_diff = (tv_start_mmce_fs.tv_sec - tv_signal_mmce_fs.tv_sec) * 1000000 + tv_start_mmce_fs.tv_usec - tv_signal_mmce_fs.tv_usec; 

    printf("[STAT] Command completed in: %liuS\n", cmd_elasped);
    //printf("MMCE FS time: %liuS\n", mmce_fs_elasped);
    //printf("Signal op -> start of op: %liuS\n", cmd_mmce_fs_diff);
    printf("\n");
}
