#include <inttypes.h>

typedef union {
    struct {
        uint16_t dirty_heap[1024];
        uint8_t dirty_map[1024]; /* every 128 byte block */
    } ps1;
    struct {
        uint16_t dirty_heap[8 * 1024 * 1024 / 512];
        uint8_t dirty_map[8 * 1024 * 1024 / 512 / 8];
    } ps2;
} bigmem_t;

extern bigmem_t bigmem;
