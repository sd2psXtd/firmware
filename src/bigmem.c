#include "bigmem.h"

bigmem_t bigmem;

#if !WITH_PSRAM
/* PS1 keeps the full card in the shared buffer on builds without PSRAM. */
uint8_t cache[128 * 1024];
_Static_assert(sizeof(cache) >= BIGMEM_CACHE_MIN_BYTES, "shared cache too small");
#endif
