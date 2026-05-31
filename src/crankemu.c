// crankemu.c


#include "app.h"
#include "dtcm.h"
#include "libcrankemu/libcrankemu.h"
#include "pdll/pdll.h"
#include "utility.h"

#include <stdarg.h>
#include <string.h>

// -- frontend callbacks (shared by every loaded core) --

static void* ce_fe_alloc_dtcm(size_t size, size_t alignment)
{
    return dtcm_alloc_aligned(size, alignment ? alignment : 1);
}

__attribute__((format(printf, 1, 2)))
static void ce_fe_set_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char* msg = NULL;
    playdate->system->vaFormatString(&msg, fmt, ap);
    va_end(ap);
    
    // TODO -- actually store this for proper user display
    // (TBD)
    
    if (msg)
    {
        playdate->system->logToConsole("emucore: %s", msg);
        playdate->system->realloc(msg, 0);
    }
}

static void ce_fe_get_buttons(PDButtons* o_down, PDButtons* o_pressed, PDButtons* o_released)
{
    if (o_down)
        *o_down = CB_App->buttons_down;
    if (o_pressed)
        *o_pressed = CB_App->buttons_pressed;
    if (o_released)
        *o_released = CB_App->buttons_released;
}

static const ce_frontend_t cb_emucore_frontend = {
    .version = CRANKEMU_VERSION,
    .alloc_dtcm = ce_fe_alloc_dtcm,
    .set_error = ce_fe_set_error,
    .get_buttons = ce_fe_get_buttons,
    .blockingModal = NULL,
};

emucore_t* CB_get_emucore_by_slug(const char* slug)
{
    if (!slug)
        return NULL;
    for (size_t i = 0; i < CB_App->cores_n; ++i)
        for (size_t j = 0; j < CB_App->cores[i].n_system_slugs; ++j)
            if (strcmp(CB_App->cores[i].system_slugs[j], slug) == 0)
                return &CB_App->cores[i];
    return NULL;
}

void CB_load_emucore(emucore_t* core)
{
    // unload the currently-active core, if any
    if (CB_App->active_emucore >= 0)
    {
        emucore_t* cur = &CB_App->cores[CB_App->active_emucore];
        if (cur->pdll)
        {
            playdate->system->logToConsole("unload core: %s", cur->id);
            pdll_close(cur->pdll);
            cur->pdll = NULL;
        }
        CB_App->active_emucore = -1;
    }

    if (!core)
        return;
    
    playdate->system->logToConsole("load core: %s", core->id);

    core->pdll = pdll_open(playdate, core->path, PDLL_FILE_PDX | PDLL_FILE_DATA);
    if (!core->pdll)
    {
        playdate->system->logToConsole("CB_load_emucore: %s", pdll_get_error());
        return;
    }
    CB_App->active_emucore = (int)(core - CB_App->cores);
    
    CB_ASSERT(CB_App->active_emucore >= 0 && CB_App->active_emucore < CB_App->cores_n);

    void (*set_frontend)(const ce_frontend_t*) = pdll_symbol(core->pdll, "ce_set_frontend");
    if (set_frontend)
        set_frontend(&cb_emucore_frontend);
    else
        playdate->system->logToConsole("CB_load_emucore: %s", pdll_get_error());
}
