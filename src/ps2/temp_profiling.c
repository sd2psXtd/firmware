#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

struct timeval tv_start_cmd, tv_end_cmd, tv_signal_mmce_fs, tv_start_mmce_fs, tv_end_mmce_fs;

void debug_profiling_stat()
{
    long cmd_elasped = (tv_end_cmd.tv_sec - tv_start_cmd.tv_sec) * 1000000 + tv_end_cmd.tv_usec - tv_start_cmd.tv_usec; 
    long mmce_fs_elasped = (tv_end_mmce_fs.tv_sec - tv_start_mmce_fs.tv_sec) * 1000000 + tv_end_mmce_fs.tv_usec - tv_start_mmce_fs.tv_usec; 
    long cmd_mmce_fs_diff = (tv_start_mmce_fs.tv_sec - tv_signal_mmce_fs.tv_sec) * 1000000 + tv_start_mmce_fs.tv_usec - tv_signal_mmce_fs.tv_usec; 

    printf("\n[[STAT]]\n");
    printf("Total time: %liuS\n", cmd_elasped);
    printf("MMCE FS time: %liuS\n", mmce_fs_elasped);
    printf("Signal operation -> start of operation: %liuS\n", cmd_mmce_fs_diff);
    printf("\n");
}
