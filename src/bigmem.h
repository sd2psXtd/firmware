#include <inttypes.h>
#include <stdint.h>

typedef union {
    struct {
        uint16_t dirty_heap[1024];
        uint8_t dirty_map[1024]; /* every 128 byte block */
    } ps1;
    #ifdef WITH_PSRAM
    struct {
        uint16_t dirty_heap[8 * 1024 * 1024 / 512];
        uint8_t dirty_map[8 * 1024 * 1024 / 512 / 8];
    } ps2;
    #endif

} bigmem_t;


extern bigmem_t bigmem;
/*
 * Shared byte cache: the PS2 page cache on PSRAM builds, the PS1 card buffer otherwise.
 * Both definitions are guaranteed to be at least this large (checked at compile time),
 * so idle phases (e.g. PS2 template card copy) may borrow it as scratch space.
 */
#define BIGMEM_CACHE_MIN_BYTES (16 * 1024)

extern uint8_t cache[];
