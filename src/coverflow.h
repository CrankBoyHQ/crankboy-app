//
//  coverflow.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef coverflow_h
#define coverflow_h

#include "array.h"
#include "game.h"

#include <pd_api.h>
#include <stdbool.h>

typedef struct CB_CoverFlow CB_CoverFlow;

// reports flow-side cover loads to the owning scene for cache accounting
typedef void (*CB_CoverFlowAccountFn)(void* ud, CB_Game* game, size_t bytes);

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
} CB_CoverFlowContext;

// Cover Flow background styles (values of preferences_library_background)
enum
{
    CB_LIB_BG_LIGHT = 0,
    CB_LIB_BG_LIGHT_GRAY,
    CB_LIB_BG_STRIPE,
    CB_LIB_BG_DARK_GRAY,
    CB_LIB_BG_DARK,
    CB_LIB_BG_COUNT
};

#define CB_LIB_BG_IS_LIGHT(bg) ((bg) <= CB_LIB_BG_LIGHT_GRAY)

// 8x8 LCDPattern for a background style; bad values clamp to Stripe
const uint8_t* cb_library_bg_pattern(int style);

CB_CoverFlow* CB_CoverFlow_new(void);
void CB_CoverFlow_free(CB_CoverFlow* cf);
void CB_CoverFlow_set_cover_accounting(
    CB_CoverFlow* cf, void* ud, CB_CoverFlowAccountFn account_loaded
);
void CB_CoverFlow_update(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx);
void CB_CoverFlow_draw(CB_CoverFlow* cf, const CB_CoverFlowContext* ctx);
void CB_CoverFlow_invalidateAll(CB_CoverFlow* cf);
void CB_CoverFlow_startSwitch(CB_CoverFlow* cf, int dir);

#endif /* coverflow_h */
