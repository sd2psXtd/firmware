#include "mcfat.h"
#include "mcfat_internal.h"

bool mcfat_cardchanged = false;
mcfat_cardspecs_t mcfat_cardspecs;
mcfat_mcops_t mcfat_bdoperations;

void mcfat_setConfig( const mcfat_mcops_t mcops, const mcfat_cardspecs_t cardspecs )
{
    mcfat_cardspecs = cardspecs;
    mcfat_bdoperations = mcops;
}

void mcfat_setCardChanged(bool changed)
{
    mcfat_cardchanged = changed;
}