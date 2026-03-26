#include "global.h"

#include "app.h"
#include "jparse.h"
#include "userstack.h"

struct global_t global;

static json_value global_to_json(void)
{
    json_value jv = json_new_table();

    json_set_table_value(&jv, "intro", json_new_bool(global.shown_intro));

    return jv;
}

static bool global_from_json(json_value jv)
{
    json_value jshown_intro = json_get_table_value(jv, "intro");
    global.shown_intro = jshown_intro.type == kJSONTrue;

    return true;
}

static bool _save_global(void)
{
    json_value jv = global_to_json();
    if (jv.type == kJSONNull)
        return false;
    write_json_to_disk(GLOBAL_FILE, jv);
    free_json_data(jv);
    return true;
}

bool save_global(void)
{
    return (bool)call_with_main_stack(_save_global);
}

bool load_global(void)
{
    json_value jv;
    if (parse_json(GLOBAL_FILE, &jv, kFileReadData) == 0)
    {
        return false;
    }
    else
    {
        if (global_from_json(jv))
        {
            free_json_data(jv);
            return true;
        }
        else
        {
            free_json_data(jv);
            return false;
        }
    }
}