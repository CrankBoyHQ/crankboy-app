#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PATCH_ENABLED 1
#define PATCH_DISABLED 0
#define PATCH_UNKNOWN -1

typedef struct SoftPatch
{
    // if NULL, indicates end to list of SoftPatch
    char* fullpath;
    char* basename;
    int state : 2;  // PATCH_ENABLED, _DISABLED, or _UNKNOWN

    // format (mutually exclusive)
    unsigned ips : 1;
    unsigned bps : 1;
    unsigned ups : 1;

    // private
    int _order : 12;
} SoftPatch;

char* get_patches_directory(const char* rom_path);
bool patches_directory_exists(const char* rom_path);
SoftPatch* list_patches(const char* rom_path, int* o_new_patch_count);
void save_patches_state(const char* rom_path, SoftPatch* patches);
void free_patches(SoftPatch* patchlist);

bool patch_rom(void** io_rom, size_t* io_romsize, const SoftPatch* patchlist);
