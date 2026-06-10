#pragma once

#include <stdbool.h>

// global settings / registry
struct global_t
{
    bool shown_intro : 1;
    const char* cores_dir;
    char* last_viewed_changelog_build;
};

extern struct global_t global;

bool save_global(void);
bool load_global(void);