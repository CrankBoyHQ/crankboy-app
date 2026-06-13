// crankemu.c

#include "../libs/libcrankemu/libcrankemu.h"
#include "../libs/pdll/pdll.h"
#include "app.h"
#include "dtcm.h"
#include "emucore_prefs.h"
#include "preferences.h"
#include "utility.h"

#include <stdarg.h>
#include <string.h>

// -- frontend callbacks (shared by every loaded core) --

static void* ce_fe_alloc_dtcm(size_t size, size_t alignment)
{
    return dtcm_alloc_aligned(size, alignment ? alignment : 1);
}

__attribute__((format(printf, 1, 2))) static void ce_fe_set_error(const char* fmt, ...)
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
        cb_free(msg);
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

static const ce_frontend_settings_t* ce_fe_settings(void)
{
    static ce_frontend_settings_t settings;
    settings.itcm_allowed = (preferences_itcm >= 1);
    settings.turbo = (preferences_uncap_fps != 0);
    return &settings;
}

static const ce_frontend_t cb_emucore_frontend = {
    .version = CRANKEMU_VERSION,
    .alloc_dtcm = ce_fe_alloc_dtcm,
    .set_error = ce_fe_set_error,
    .get_buttons = ce_fe_get_buttons,
    .blockingModal = NULL,
    .get_hardware_revision = NULL,
    .settings = ce_fe_settings,
};

void cb_apply_persisted_emucore_prefs(emucore_t* core, const char* slug)
{
    if (!core || !core->pdll || core->n_system_slugs == 0)
        return;
    ce_preference_t** (*get_prefs)(void) = pdll_symbol(core->pdll, "ce_get_preferences");
    if (!get_prefs)
        return;
    ce_preference_t** prefs = get_prefs();
    if (!prefs)
        return;
    size_t n_prefs = cb_nullterm_array_len((void* const*)prefs);
    for (size_t i = 0; i < n_prefs; ++i)
    {
        ce_preference_t* pref = prefs[i];
        if (pref->type != CE_PREFERENCE_STANDARD || !pref->id || !pref->set)
            continue;
        char key[96];
        snprintf(key, sizeof(key), "%s:%s", slug, pref->id);
        uint32_t flags = pref->flags ? pref->flags(pref) : 0;
        unsigned value = 0;
        bool found = false;
        if (!(flags & CE_PREF_ALWAYS_GLOBAL))
            found = cb_emucore_prefs_get_local(key, &value);
        if (!found && !(flags & CE_PREF_ALWAYS_LOCAL))
            found = cb_emucore_prefs_get_global(key, &value);
        if (!found)
            continue;
        size_t count = cb_nullterm_array_len((void* const*)pref->values);
        if (count == 0 || value >= count)
            continue;
        pref->set(pref, value);
    }
}

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

void cb_emucore_set_frontend(pdll_t* pdll)
{
    if (!pdll)
        return;
    void (*set_frontend)(const ce_frontend_t*) = pdll_symbol(pdll, "ce_set_frontend");
    if (set_frontend)
        set_frontend(&cb_emucore_frontend);
    else
        playdate->system->logToConsole("cb_emucore_set_frontend: %s", pdll_get_error());
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

    core->pdll = pdll_open(playdate, core->path, PDLL_FILE_PDX | PDLL_FILE_DATA, 1 << 16);
    if (!core->pdll)
    {
        playdate->system->logToConsole("CB_load_emucore: %s", pdll_get_error());
        return;
    }
    // in principle, this can be used to reconcile dynamically linked offsets
    // when tracing...
    playdate->system->logToConsole(
        "core image base: id=%s base=0x%08lx path=%s", core->id,
        (unsigned long)(uintptr_t)core->pdll->image, core->path
    );
    CB_App->active_emucore = (int)(core - CB_App->cores);

    CB_ASSERT(CB_App->active_emucore >= 0 && CB_App->active_emucore < CB_App->cores_n);

    cb_emucore_set_frontend(core->pdll);
}
