#include "bigmem.h"

bigmem_t bigmem;
uint8_t cache[CACHE_SIZE];
uint8_t dirty_flushbuf[DIRTY_FLUSHBUF_SIZE];
