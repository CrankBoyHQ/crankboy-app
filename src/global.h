#pragma once

#include <stdbool.h>

// global settings / registry
struct global_t
{
    bool shown_intro : 1;
};

extern struct global_t global;

bool save_global(void);
bool load_global(void);