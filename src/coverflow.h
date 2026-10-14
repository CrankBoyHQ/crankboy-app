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
    const char* text;
    int hintWidth;        // 0 = measure
    const LCDFont* font;  // NULL = subhead
    int gapBefore;
} CB_FlowLine;

typedef struct
{
    CB_Array* games;
    int itemCount;
    int* selection;
    bool forceRedraw;
    CB_FlowLine placeholder[6];  // empty-cover status lines, filled by the caller per state
    bool placeholderAnimated;    // download dots animate -> redraw every frame
    const char* categoryName;    // NULL = hide
    const char* trailingTitle;   // trailing "Get ROMs..." card
    int xShift;                  // launch slide
} CB_CoverFlowContext;

CB_CoverFlow* CB_CoverFlow_new(void);
void CB_CoverFlow_free(CB_CoverFlow* cf);
void CB_CoverFlow_update(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx);
void CB_CoverFlow_draw(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx);
void CB_CoverFlow_invalidateAll(CB_CoverFlow* cf);
void CB_CoverFlow_startSwitch(CB_CoverFlow* cf, int dir);

#endif /* coverflow_h */
