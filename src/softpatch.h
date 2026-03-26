#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PATCH_ENABLED 1
#define PATCH_DISABLED 0
#define PATCH_UNKNOWN -1

typedef struct SoftPatch
{
    // if NULL, indicates end to list of SoftPatch
    char* fullpath;

    // extension-stripped basename
    char* basename;
    int state : 2;  // PATCH_ENABLED, _DISABLED, or _UNKNOWN

    // format (mutually exclusive)
    unsigned ips : 1;
    unsigned bps : 1;
    unsigned ups : 1;

    // private
    int _order : 12;
} SoftPatch;

// extension should start with '.'
bool extension_is_supported_patch_file(const char* extension);
bool extension_is_unsupported_patch_file(const char* extension);
char* get_patches_directory(const char* rom_path);
bool patches_directory_exists(const char* rom_path);
SoftPatch* list_patches(const char* rom_path, int* o_new_patch_count);
void save_patches_state(const char* rom_path, SoftPatch* patches);
void free_patches(SoftPatch* patchlist);

// calculates a hash based off the *basenames* of the patches.
// not dependent on patch order.
uint32_t patch_hash(SoftPatch* patches);

bool patch_rom(void** io_rom, size_t* io_romsize, const SoftPatch* patchlist);
