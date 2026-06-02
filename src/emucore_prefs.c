#include "emucore_prefs.h"

#include "jparse.h"
#include "utility.h"

#include <string.h>

typedef struct
{
    char* key;
    unsigned value;
} cb_emucore_pref_entry;

typedef struct
{
    cb_emucore_pref_entry* items;
    size_t n;
    size_t cap;
} cb_emucore_pref_dict;

static cb_emucore_pref_dict g_global;
static cb_emucore_pref_dict g_local;

static cb_emucore_pref_dict* dict_for(bool is_global)
{
    return is_global ? &g_global : &g_local;
}

static int dict_find(cb_emucore_pref_dict* d, const char* key)
{
    for (size_t i = 0; i < d->n; ++i)
        if (strcmp(d->items[i].key, key) == 0)
            return (int)i;
    return -1;
}

static void dict_clear(cb_emucore_pref_dict* d)
{
    for (size_t i = 0; i < d->n; ++i)
        cb_free(d->items[i].key);
    cb_free(d->items);
    d->items = NULL;
    d->n = 0;
    d->cap = 0;
}

static void dict_set(cb_emucore_pref_dict* d, const char* key, unsigned value)
{
    int idx = dict_find(d, key);
    if (idx >= 0)
    {
        d->items[idx].value = value;
        return;
    }
    if (d->n == d->cap)
    {
        size_t newcap = d->cap ? d->cap * 2 : 8;
        cb_emucore_pref_entry* p = cb_realloc(d->items, newcap * sizeof(*p));
        if (!p)
            return;
        d->items = p;
        d->cap = newcap;
    }
    d->items[d->n].key = cb_strdup(key);
    if (!d->items[d->n].key)
        return;
    d->items[d->n].value = value;
    d->n++;
}

static bool key_has_slug(const char* key)
{
    return key && strchr(key, ':') != NULL;
}

static void load_from_json_table(json_value tbl, bool is_global)
{
    if (tbl.type != kJSONTable)
        return;
    JsonObject* obj = tbl.data.tableval;
    cb_emucore_pref_dict* d = dict_for(is_global);
    for (size_t i = 0; i < obj->n; ++i)
    {
        const char* k = obj->data[i].key;
        if (!key_has_slug(k))
            continue;
        if (obj->data[i].value.type != kJSONInteger)
            continue;
        dict_set(d, k, obj->data[i].value.data.intval);
    }
}

void cb_emucore_prefs_init(void)
{
    dict_clear(&g_global);
    dict_clear(&g_local);
}

int cb_emucore_prefs_read_from_disk(const char* filename, bool is_global)
{
    dict_clear(dict_for(is_global));
    if (!filename)
        return 0;
    json_value j;
    if (!parse_json(filename, &j, kFileReadData))
    {
        return 0;
    }
    if (j.type == kJSONTable)
        load_from_json_table(j, is_global);
    free_json_data(j);
    return 1;
}

int cb_emucore_prefs_save_to_disk(const char* filename, bool is_global)
{
    if (!filename)
        return 0;
    cb_emucore_pref_dict* d = dict_for(is_global);

    json_value root;
    if (!parse_json(filename, &root, kFileReadData) || root.type != kJSONTable)
    {
        if (root.type != kJSONNull)
            free_json_data(root);
        root = json_new_table();
        if (root.type != kJSONTable)
            return 0;
    }

    for (size_t i = 0; i < d->n; ++i)
        json_set_table_value(&root, d->items[i].key, json_new_int(d->items[i].value));

    int err = write_json_to_disk(filename, root);
    free_json_data(root);
    return !err;
}

bool cb_emucore_prefs_get_global(const char* key, unsigned* out)
{
    int idx = dict_find(&g_global, key);
    if (idx < 0)
        return false;
    if (out)
        *out = g_global.items[idx].value;
    return true;
}

bool cb_emucore_prefs_get_local(const char* key, unsigned* out)
{
    int idx = dict_find(&g_local, key);
    if (idx < 0)
        return false;
    if (out)
        *out = g_local.items[idx].value;
    return true;
}

void cb_emucore_prefs_set_global(const char* key, unsigned value)
{
    if (!key_has_slug(key))
        return;
    dict_set(&g_global, key, value);
}

void cb_emucore_prefs_set_local(const char* key, unsigned value)
{
    if (!key_has_slug(key))
        return;
    dict_set(&g_local, key, value);
}