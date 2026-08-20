//
//  coverflow.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef coverflow_h
#define coverflow_h

#include "array.h"

#include <pd_api.h>
#include <stdbool.h>

typedef struct CB_CoverFlow CB_CoverFlow;

typedef struct
{
    CB_Array* games;
    int itemCount;
    int* selection;
    bool forceRedraw;
    const char* statusText;
} CB_CoverFlowContext;

CB_CoverFlow* CB_CoverFlow_new(void);
void CB_CoverFlow_free(CB_CoverFlow* cf);
void CB_CoverFlow_update(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx);
void CB_CoverFlow_draw(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx);
void CB_CoverFlow_invalidateAll(CB_CoverFlow* cf);

#endif /* coverflow_h */
