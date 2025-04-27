#include <inttypes.h>
#include <stdint.h>

#if WITH_PSRAM
    #define CACHE_SIZE  512 * 45
#else
    #define CACHE_SIZE 1024 * 128
#endif
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
extern uint8_t cache[CACHE_SIZE];
