#ifndef _MCFAT_H
#define _MCFAT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct _mcfat_cardspecs
{
    uint16_t pagesize;
    uint16_t blocksize;
    int32_t cardsize;
    uint8_t flags;
} mcfat_cardspecs_t;

typedef struct _mc_ops
{
    int (*page_erase)(mcfat_cardspecs_t*, uint32_t);
    int (*page_write)(mcfat_cardspecs_t*, uint32_t, void*);
    int (*page_read)(mcfat_cardspecs_t*, uint32_t, uint32_t, void*);
    int (*ecc_write)(mcfat_cardspecs_t*, uint32_t, void*);
    int (*ecc_read)(mcfat_cardspecs_t*, uint32_t, uint32_t, void*);
} mcfat_mcops_t;


void mcfat_setConfig( const mcfat_mcops_t mcops, const mcfat_cardspecs_t cardspecs );
void mcfat_setCardChanged( bool changed );

#endif