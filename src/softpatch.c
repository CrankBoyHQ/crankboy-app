#include "softpatch.h"

#include "app.h"
#include "jparse.h"
#include "pd_api.h"
#include "userstack.h"
#include "utility.h"

#include <stdint.h>

char* get_patches_directory(const char* rom_path)
{
    char* bn = cb_basename(rom_path, true);
    char* f = aprintf("%s/%s", CB_patchesPath, bn);

    cb_free(bn);
    return f;
}

bool patches_directory_exists(const char* rom_path)
{
    char* dir = get_patches_directory(rom_path);

    FileStat stat;
    int result = playdate->file->stat(dir, &stat);
    cb_free(dir);

    if (result != 0)
        return false;
    if (!stat.isdir)
        return false;
    return true;
}

struct ListPatchAcc
{
    SoftPatch* list;
    const char* patch_dir;
};

void list_patch_cb(const char* filename, void* ud)
{
    struct ListPatchAcc* acc = ud;

    int n = 0;
    for (SoftPatch* patch = acc->list; patch && patch->fullpath; ++patch, ++n)
        ;

    bool ips = endswithi(filename, ".ips");
    bool bps = endswithi(filename, ".bps");
    bool ups = endswithi(filename, ".ups");

    if (ips || bps || ups)
    {
        // add new one
        acc->list = playdate->system->realloc(acc->list, sizeof(SoftPatch) * (n + 2));
        acc->list[n + 1].fullpath = NULL;  // terminal
        acc->list[n + 1].basename = NULL;  // terminal

        SoftPatch* patch = &acc->list[n];
        patch->fullpath = aprintf("%s/%s", acc->patch_dir, filename);
        patch->basename = cb_basename(filename, true);
        patch->ips = ips;
        patch->bps = bps;
        patch->ups = ups;
        patch->state = PATCH_UNKNOWN;
        patch->_order = -1;
    }
}

static char* patch_list_file(const char* patch_dir)
{
    return aprintf("%s/%s", patch_dir, PATCH_LIST_FILE);
}

SoftPatch* list_patches(const char* rom_path, int* new_patch_count)
{
    char* patch_dir = get_patches_directory(rom_path);

    // Check if the patches directory exists.
    FileStat stat;
    if (playdate->file->stat(patch_dir, &stat) != 0 || !stat.isdir)
    {
        cb_free(patch_dir);
        if (new_patch_count)
            *new_patch_count = 0;
        return NULL;
    }

    struct ListPatchAcc acc;
    acc.list = NULL;
    acc.patch_dir = patch_dir;

    playdate->file->listfiles(patch_dir, list_patch_cb, &acc, false);
    char* listpath = patch_list_file(patch_dir);
    cb_free(patch_dir);

    json_value jv;
    parse_json(listpath, &jv, kFileReadData);
    cb_free(listpath);
    json_value jpatches = json_get_table_value(jv, "patches");
    int nextorder = 0;
    if (jpatches.type == kJSONArray)
    {
        JsonArray* arr = jpatches.data.arrayval;
        for (size_t i = 0; i < arr->n; ++i)
        {
            json_value entry = arr->data[i];
            if (entry.type == kJSONTable)
            {
                json_value jbasename = json_get_table_value(entry, "basename");
                json_value jorder = json_get_table_value(entry, "n");
                json_value jenabled = json_get_table_value(entry, "enabled");

                if (jbasename.type != kJSONString || jorder.type != kJSONInteger)
                    continue;

                // find matching patch
                for (SoftPatch* patch = acc.list; patch && patch->fullpath; ++patch)
                {
                    if (!strcmp(jbasename.data.stringval, patch->basename))
                    {
                        patch->state =
                            (jenabled.type == kJSONTrue) ? PATCH_ENABLED : PATCH_DISABLED;
                        patch->_order = jorder.data.intval;
                        nextorder = MAX(patch->_order + 1, nextorder);
                        break;
                    }
                }
            }
        }
    }

    free_json_data(jpatches);

    if (new_patch_count)
        *new_patch_count = 0;

    // assign an _order value to any unlisted patches
    size_t len = 0;
    for (SoftPatch* patch = acc.list; patch && patch->fullpath; ++patch)
    {
        if (patch->_order < 0)
        {
            patch->_order = nextorder++;
            if (new_patch_count)
            {
                (*new_patch_count)++;
            }
        }
        ++len;
    }

    // insertion-sort by _order value.
    if (len > 1)
    {
        for (size_t i = 1; i < len; i++)
        {
            size_t j = i;
            while (j > 0 && (acc.list[j]._order < acc.list[j - 1]._order))
            {
                memswap(&acc.list[j], &acc.list[j - 1], sizeof(SoftPatch));
                j--;
            }
        }
    }

    return acc.list;
}

void save_patches_state(const char* rom_path, SoftPatch* patches)
{
    if (!patches)
        return;

    // number of patches
    size_t len = 0;
    for (SoftPatch* patch = patches; patch->fullpath; ++patch, ++len)
        ;
    if (len == 0)
        return;

    JsonArray* jpatcharray = cb_malloc(sizeof(JsonArray) + len * sizeof(json_value));
    if (!jpatcharray)
        return;
    jpatcharray->n = len;

    json_value jmanifest = json_new_table();
    json_value jpatches = {.type = kJSONArray};
    jpatches.data.arrayval = jpatcharray;
    json_set_table_value(&jmanifest, "patches", jpatches);

    for (size_t i = 0; i < len; ++i)
    {
        SoftPatch* patch = &patches[i];
        json_value jpatch = json_new_table();
        json_set_table_value(&jpatch, "basename", json_new_string(patch->basename));
        json_set_table_value(&jpatch, "n", json_new_int((int)i));
        if (patch->state == PATCH_ENABLED)
        {
            json_set_table_value(&jpatch, "enabled", json_new_bool(true));
        }

        jpatcharray->data[i] = jpatch;
    }

    char* dir = get_patches_directory(rom_path);
    playdate->file->mkdir(dir);
    char* plf = patch_list_file(dir);
    cb_free(dir);
    printf("saving patches state to %s...\n", plf);
    write_json_to_disk(plf, jmanifest);
    cb_free(plf);

    free_json_data(jmanifest);
}

void free_patches(SoftPatch* patchlist)
{
    if (!patchlist)
        return;

    for (SoftPatch* patch = patchlist; patch->fullpath; ++patch)
    {
        cb_free(patch->fullpath);
        if (patch->basename)
            cb_free(patch->basename);
    }
    cb_free(patchlist);
}

unsigned read_bigendian(void* src, int bytes)
{
    unsigned x = 0;
    while (bytes-- > 0)
    {
        x <<= 8;
        x |= *(uint8_t*)(src++);
    }
    return x;
}

static uint32_t read_littleendian_u32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

#define IPS_MAGIC "PATCH"
#define IPS_EOF 0x454F46 /* "EOF" */

static bool apply_ips_patch(void** rom, size_t* romsize, const SoftPatch* patch)
{
    size_t ips_len;
    void* ips_original_buffer =
        call_with_main_stack_3(cb_read_entire_file, patch->fullpath, &ips_len, kFileReadData);

    if (!ips_original_buffer)
    {
        playdate->system->error("Unable to open IPS patch \"%s\"", patch->fullpath);
        return false;
    }

    void* ips = ips_original_buffer;

#define ADVANCE(X)          \
    do                      \
    {                       \
        unsigned __x = (X); \
        ips += __x;         \
        ips_len -= __x;     \
    } while (0)
#define CHECKLEN(l)        \
    do                     \
    {                      \
        if (ips_len < (l)) \
            goto err;      \
    } while (0)

    if (memcmp(ips, IPS_MAGIC, 5))
    {
        goto err;
    }
    ADVANCE(5);

    while (ips_len > 0)
    {
        CHECKLEN(3);
        unsigned offset = read_bigendian(ips, 3);
        ADVANCE(3);

        if (offset == IPS_EOF)
        {
            if (ips_len == 3)
            {
                unsigned new_size = read_bigendian(ips, 3);
                if (new_size < *romsize)
                {
                    void* resized_rom = cb_realloc(*rom, new_size);
                    if (!resized_rom && new_size > 0)
                    {
                        playdate->system->error(
                            "IPS patch failed to truncate ROM: not enough memory."
                        );
                        cb_free(ips_original_buffer);
                        return false;
                    }
                    *rom = resized_rom;
                    *romsize = new_size;
                }
            }
            break;
        }

        bool rle = false;
        CHECKLEN(2);
        unsigned length = read_bigendian(ips, 2);
        ADVANCE(2);

        if (length == 0)
        {
            CHECKLEN(2);
            length = read_bigendian(ips, 2);
            ADVANCE(2);
            rle = true;
        }

        if (offset + length > *romsize)
        {
            *romsize = offset + length;
            *rom = cb_realloc(*rom, *romsize);
            if (!*rom)
            {
                playdate->system->error(
                    "IPS patch requires ROM to be resized, but there was not enough memory."
                );
                cb_free(ips_original_buffer);
                return false;
            }
        }

        if (rle)
        {
            // run-length encoded hunk
            CHECKLEN(1);
            unsigned v = read_bigendian(ips, 1);
            ADVANCE(1);

            // RLE record
            memset((uint8_t*)*rom + offset, v, length);
        }
        else
        {
            CHECKLEN(length);
            // Standard record
            memcpy((uint8_t*)*rom + offset, ips, length);
            ADVANCE(length);
        }
    }

    cb_free(ips_original_buffer);
    return true;

err:
    playdate->system->error("Error applying IPS patch \"%s\"", patch->fullpath);
    cb_free(ips_original_buffer);
    return false;
}

/*
 * ============================================================================
 * UPS Patching Implementation
 * ============================================================================
 * The logic for parsing and applying UPS patches, particularly the handling
 * of the non-standard VLQ and checksums, is based on the JavaScript
 * implementation by Marc Robledo.
 *
 * Original Project: RomPatcher.js
 * Author: Marc Robledo
 * Source: https://github.com/marcrobledo/RomPatcher.js
 * ============================================================================
 */

#define UPS_MAGIC "UPS1"

static uint64_t read_ups_vlq(const uint8_t** data, size_t* size)
{
    uint64_t result = 0;
    uint64_t shift = 0;
    while (*size > 0)
    {
        uint8_t byte = **data;
        (*data)++;
        (*size)--;

        uint64_t part = byte & 0x7F;
        result += part << shift;

        if (byte & 0x80)
        {
            return result;
        }

        shift += 7;
        result += (1ULL << shift);
    }
    return (uint64_t)-1;
}

static bool apply_ups_patch(void** rom, size_t* romsize, const SoftPatch* patch)
{
    size_t ups_len;
    uint8_t* ups_data = (uint8_t*)call_with_main_stack_3(
        cb_read_entire_file, patch->fullpath, &ups_len, kFileReadData
    );
    if (!ups_data)
    {
        playdate->system->error("Unable to open UPS patch \"%s\"", patch->fullpath);
        return false;
    }

    const uint8_t* ups_ptr = ups_data;
    size_t ups_remaining = ups_len;
    uint8_t* new_rom = NULL;
    bool success = false;

    if (ups_remaining < 16)
        goto err_corrupt;

    // Verify patch
    uint32_t patch_checksum_from_file = read_littleendian_u32(ups_data + ups_len - 4);
    uint32_t calculated_patch_checksum = crc32_for_buffer(ups_data, ups_len - 4);
    if (patch_checksum_from_file != calculated_patch_checksum)
    {
        goto err_corrupt;
    }

    if (memcmp(ups_ptr, UPS_MAGIC, 4) != 0)
        goto err_corrupt;
    ups_ptr += 4;
    ups_remaining -= 4;

    uint64_t input_size_from_patch = read_ups_vlq(&ups_ptr, &ups_remaining);
    uint64_t output_size_from_patch = read_ups_vlq(&ups_ptr, &ups_remaining);

    if (input_size_from_patch == (uint64_t)-1 || output_size_from_patch == (uint64_t)-1)
        goto err_corrupt;

    // Verify input ROM
    size_t effective_rom_size = *romsize;
    if (input_size_from_patch != *romsize)
    {
        if (input_size_from_patch < *romsize)
        {
            playdate->system->logToConsole(
                "UPS warning: Patch expects size %llu, ROM is %zu. Treating as overdump/headered.",
                input_size_from_patch, *romsize
            );
            effective_rom_size = input_size_from_patch;
        }
        else
        {
            playdate->system->error(
                "UPS error: Patch expects ROM of size %llu, but current ROM is size %zu.",
                input_size_from_patch, *romsize
            );
            goto cleanup;
        }
    }

    uint32_t input_checksum_from_patch = read_littleendian_u32(ups_data + ups_len - 12);
    uint32_t calculated_input_checksum = crc32_for_buffer(*rom, effective_rom_size);

    if (input_checksum_from_patch != calculated_input_checksum)
    {
        playdate->system->error("Input ROM checksum mismatch. The patch is not for this ROM.");
        goto cleanup;
    }

    new_rom = cb_malloc(output_size_from_patch);
    if (!new_rom)
    {
        playdate->system->error("Failed to allocate memory for patched ROM.");
        goto cleanup;
    }

    size_t copy_len = (*romsize < output_size_from_patch) ? *romsize : output_size_from_patch;
    memcpy(new_rom, *rom, copy_len);
    if (output_size_from_patch > *romsize)
    {
        memset(new_rom + *romsize, 0, output_size_from_patch - *romsize);
    }

    size_t current_pos = 0;

    while (ups_remaining > 12)
    {
        uint64_t relative_offset = read_ups_vlq(&ups_ptr, &ups_remaining);
        current_pos += relative_offset;

        while (ups_remaining > 12)
        {
            uint8_t xor_byte = *ups_ptr++;
            ups_remaining--;

            if (xor_byte == 0)
            {
                current_pos++;
                break;
            }

            if (current_pos >= output_size_from_patch)
                goto err_bounds;

            new_rom[current_pos] ^= xor_byte;
            current_pos++;
        }
    }

    // Verify output ROM
    uint32_t output_checksum_from_patch = read_littleendian_u32(ups_data + ups_len - 8);
    uint32_t calculated_output_checksum = crc32_for_buffer(new_rom, output_size_from_patch);

    if (output_checksum_from_patch != calculated_output_checksum)
    {
        playdate->system->error("UPS error: Output ROM checksum mismatch. Patching failed.");
        cb_free(new_rom);
        goto cleanup;
    }

    cb_free(*rom);
    *rom = new_rom;
    *romsize = output_size_from_patch;
    success = true;
    goto cleanup;

err_bounds:
    playdate->system->error("UPS error: Patch tried to write out of bounds.");
    if (new_rom)
        cb_free(new_rom);
    goto cleanup;

err_corrupt:
    playdate->system->error("UPS error: Patch file is corrupt or invalid.");
    if (new_rom)
        cb_free(new_rom);

cleanup:
    if (ups_data)
        cb_free(ups_data);
    return success;
}

/*
 * ============================================================================
 * BPS Patching Implementation
 * ============================================================================
 * Implementation based on the official BPS specification by byuu
 * and the JavaScript implementation by Marc Robledo.
 *
 * Original Project: RomPatcher.js
 * Author: Marc Robledo
 * Source: https://github.com/marcrobledo/RomPatcher.js
 * Spec: https://www.romhacking.net/documents/746/
 * ============================================================================
 */

#define BPS_MAGIC "BPS1"
#define BPS_ACTION_SOURCE_READ 0
#define BPS_ACTION_TARGET_READ 1
#define BPS_ACTION_SOURCE_COPY 2
#define BPS_ACTION_TARGET_COPY 3

static uint64_t read_bps_vlq(const uint8_t** data, size_t* size)
{
    uint64_t result = 0, shift = 1;
    while (*size > 0)
    {
        uint8_t x = **data;
        (*data)++;
        (*size)--;
        result += (x & 0x7f) * shift;
        if (x & 0x80)
            break;
        shift <<= 7;
        result += shift;
    }
    return result;
}

static bool apply_bps_patch(void** rom, size_t* romsize, const SoftPatch* patch)
{
    size_t bps_len;
    uint8_t* bps_data = (uint8_t*)call_with_main_stack_3(
        cb_read_entire_file, patch->fullpath, &bps_len, kFileReadData
    );
    if (!bps_data)
    {
        playdate->system->error("Unable to open BPS patch \"%s\"", patch->fullpath);
        return false;
    }

    const uint8_t* bps_ptr = bps_data;
    size_t bps_remaining = bps_len;
    uint8_t* new_rom = NULL;
    bool success = false;

    if (bps_remaining < 16)
        goto err_corrupt;

    uint32_t patch_checksum_from_file = read_littleendian_u32(bps_data + bps_len - 4);
    uint32_t calculated_patch_checksum = crc32_for_buffer(bps_data, bps_len - 4);
    if (patch_checksum_from_file != calculated_patch_checksum)
    {
        goto err_corrupt;
    }

    if (memcmp(bps_ptr, BPS_MAGIC, 4) != 0)
        goto err_corrupt;
    bps_ptr += 4;
    bps_remaining -= 4;

    uint64_t source_size_from_patch = read_bps_vlq(&bps_ptr, &bps_remaining);
    uint64_t target_size_from_patch = read_bps_vlq(&bps_ptr, &bps_remaining);
    uint64_t metadata_len = read_bps_vlq(&bps_ptr, &bps_remaining);

    if (metadata_len > bps_remaining)
        goto err_corrupt;
    bps_ptr += metadata_len;
    bps_remaining -= metadata_len;

    if (source_size_from_patch != *romsize)
    {
        playdate->system->error(
            "BPS error: Input ROM size mismatch. Expected %llu, got %zu.", source_size_from_patch,
            *romsize
        );
        goto cleanup;
    }

    uint32_t source_checksum_from_patch = read_littleendian_u32(bps_data + bps_len - 12);
    uint32_t calculated_source_checksum = crc32_for_buffer(*rom, *romsize);
    if (source_checksum_from_patch != calculated_source_checksum)
    {
        playdate->system->error("BPS error: Input ROM checksum mismatch.");
        goto cleanup;
    }

    new_rom = cb_malloc(target_size_from_patch);
    if (!new_rom)
    {
        playdate->system->error("BPS error: Failed to allocate memory for patched ROM.");
        goto cleanup;
    }

    size_t output_offset = 0;
    size_t source_linear_offset = 0;
    int64_t source_relative_offset = 0;
    int64_t target_relative_offset = 0;

    while (bps_remaining > 12)
    {
        uint64_t data = read_bps_vlq(&bps_ptr, &bps_remaining);
        uint32_t command = data & 3;
        uint64_t length = (data >> 2) + 1;

        if (output_offset + length > target_size_from_patch)
            goto err_bounds;

        switch (command)
        {
        case BPS_ACTION_SOURCE_READ:
        {
            if (output_offset + length > *romsize)
                goto err_bounds;
            memcpy(new_rom + output_offset, (uint8_t*)*rom + output_offset, length);
            output_offset += length;
            break;
        }
        case BPS_ACTION_TARGET_READ:
        {
            if (length > bps_remaining)
                goto err_corrupt;
            memcpy(new_rom + output_offset, bps_ptr, length);
            bps_ptr += length;
            bps_remaining -= length;
            output_offset += length;
            break;
        }
        case BPS_ACTION_SOURCE_COPY:
        {
            uint64_t offset_data = read_bps_vlq(&bps_ptr, &bps_remaining);
            int64_t relative_offset = (offset_data & 1 ? -1 : 1) * (offset_data >> 1);

            source_relative_offset += relative_offset;
            if (source_relative_offset < 0 ||
                (uint64_t)(source_relative_offset + length) > *romsize)
                goto err_bounds;

            memcpy(new_rom + output_offset, (uint8_t*)*rom + source_relative_offset, length);

            source_relative_offset += length;
            output_offset += length;
            break;
        }
        case BPS_ACTION_TARGET_COPY:
        {
            uint64_t offset_data = read_bps_vlq(&bps_ptr, &bps_remaining);
            int64_t relative_offset = (offset_data & 1 ? -1 : 1) * (offset_data >> 1);

            target_relative_offset += relative_offset;
            if (target_relative_offset < 0 || (uint64_t)target_relative_offset >= output_offset)
                goto err_bounds;

            for (uint64_t i = 0; i < length; i++)
            {
                new_rom[output_offset++] = new_rom[target_relative_offset++];
            }
            break;
        }
        }
    }

    uint32_t target_checksum_from_patch = read_littleendian_u32(bps_data + bps_len - 8);
    uint32_t calculated_target_checksum = crc32_for_buffer(new_rom, target_size_from_patch);
    if (target_checksum_from_patch != calculated_target_checksum)
    {
        playdate->system->error("BPS error: Output ROM checksum mismatch. Patching failed.");
        cb_free(new_rom);
        goto cleanup;
    }

    cb_free(*rom);
    *rom = new_rom;
    *romsize = target_size_from_patch;
    success = true;
    goto cleanup;

err_bounds:
    playdate->system->error("BPS error: Patch tried to write out of bounds.");
    if (new_rom)
        cb_free(new_rom);
    goto cleanup;

err_corrupt:
    playdate->system->error("BPS error: Patch file is corrupt or invalid.");
    if (new_rom)
        cb_free(new_rom);

cleanup:
    if (bps_data)
        cb_free(bps_data);
    return success;
}

bool patch_rom(void** rom, size_t* romsize, const SoftPatch* patchlist)
{
    bool success = true;
    for (const SoftPatch* patch = patchlist; patch && patch->fullpath; patch++)
    {
        if (patch->state != PATCH_ENABLED)
            continue;

        printf("Applying %s...\n", patch->fullpath);

        if (patch->ips)
        {
            success = success && apply_ips_patch(rom, romsize, patch);
        }
        else if (patch->ups)
        {
            success = success && apply_ups_patch(rom, romsize, patch);
        }
        else if (patch->bps)
        {
            success = success && apply_bps_patch(rom, romsize, patch);
        }
        else
        {
            // This case should ideally not be reached if flags are set correctly
            success = false;
            playdate->system->error("Unknown patch type for %s", patch->fullpath);
        }
    }
    return success;
}
