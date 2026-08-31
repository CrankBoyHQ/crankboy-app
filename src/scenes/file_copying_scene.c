//
//  scenes/file_copying_scene.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#include "file_copying_scene.h"

#include "../app.h"
#include "../jparse.h"
#include "../utility.h"
#include "game_scanning_scene.h"

void CB_FileCopyingScene_update(void* object, uint32_t u32enc_dt);
void CB_FileCopyingScene_free(void* object);

typedef struct
{
    char* full_path;
    char* filename;
} FileToCopy;

static int filename_parse_version(const char* filename, char** out_stripped)
{
    if (out_stripped)
        *out_stripped = NULL;

    const char* at = strrchr(filename, '@');
    if (!at)
        return 0;
    if (at[1] < '1' || at[1] > '9')
        return 0;

    int version = 0;
    const char* p = at + 1;
    while (*p >= '0' && *p <= '9')
    {
        version = version * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0')
        return 0;

    if (out_stripped)
    {
        size_t base_len = at - filename;
        *out_stripped = cb_malloc(base_len + 1);
        memcpy(*out_stripped, filename, base_len);
        (*out_stripped)[base_len] = '\0';
    }

    return version;
}

struct list_files_ud
{
    CB_FileCopyingScene* scene;
    const char* directory;
};

static bool copy_one_file(const char* full_path, const char* filename)
{
    char* dst_path = NULL;

    if (cb_file_has_extension(filename, ".png") || cb_file_has_extension(filename, ".jpg") ||
        cb_file_has_extension(filename, ".jpeg") || cb_file_has_extension(filename, ".bmp") ||
        cb_file_has_extension(filename, ".pdi"))
    {
        dst_path = aprintf("%s/%s", cb_gb_directory_path(CB_coversPath), filename);
    }
    else if (cb_file_has_extension(filename, ".gb") || cb_file_has_extension(filename, ".gbc"))
    {
        dst_path = aprintf("%s/%s", cb_gb_directory_path(CB_gamesPath), filename);
    }
    else if (cb_file_has_extension(filename, ".gbz"))
    {
        dst_path = aprintf("%s/%s", cb_gb_directory_path(CB_gamesPath), filename);
    }
    else if (cb_file_has_extension(filename, ".sav"))
    {
        dst_path = aprintf("%s/%s", cb_gb_directory_path(CB_savesPath), filename);
    }
    else if (cb_file_has_extension(filename, ".state"))
    {
        dst_path = aprintf("%s/%s", cb_gb_directory_path(CB_statesPath), filename);
    }

    if (!dst_path)
    {
        return false;
    }

    size_t size;
    void* dat = cb_read_entire_file(full_path, &size, kFileRead);

    bool success = false;
    if (dat)
    {
        success = cb_write_entire_file(dst_path, dat, size);
        if (!success)
        {
            playdate->system->logToConsole("Error: Failed to write to %s", dst_path);
        }
    }
    else
    {
        playdate->system->logToConsole("Error: Failed to read from %s", full_path);
    }

    cb_free(dat);
    cb_free(dst_path);
    return success;
}

static void collect_files_callback(const char* filename, void* userdata)
{
    struct list_files_ud* ud = userdata;
    CB_FileCopyingScene* scene = ud->scene;

    char* full_path = aprintf("%s/%s", ud->directory, filename);
    if (!full_path)
        return;

    json_value already_copied = json_get_table_value(scene->manifest, full_path);
    if (already_copied.type == kJSONTrue)
    {
        cb_free(full_path);
        return;
    }

    char* stripped_filename = NULL;
    filename_parse_version(filename, &stripped_filename);
    const char* name_for_ext = stripped_filename ? stripped_filename : filename;
    bool should_copy = false;

    if (cb_file_has_extension(name_for_ext, ".png") ||
        cb_file_has_extension(name_for_ext, ".jpg") ||
        cb_file_has_extension(name_for_ext, ".jpeg") || cb_file_has_extension(name_for_ext, ".bmp"))
    {
        should_copy = true;
    }
#ifndef CRANKBOY_OFFICIAL_CATALOG
    else if (
        cb_file_has_extension(name_for_ext, ".pdi") || cb_file_has_extension(name_for_ext, ".gb") ||
        cb_file_has_extension(name_for_ext, ".gbc") ||
        cb_file_has_extension(name_for_ext, ".gbz") ||
        cb_file_has_extension(name_for_ext, ".sav") || cb_file_has_extension(name_for_ext, ".state")
    )
    {
        should_copy = true;
    }
#endif

    if (should_copy)
    {
        FileToCopy* file_to_copy = cb_malloc(sizeof(FileToCopy));
        file_to_copy->full_path = full_path;
        file_to_copy->filename = stripped_filename ? stripped_filename : cb_strdup(filename);
        array_push(scene->files_to_copy, file_to_copy);
    }
    else
    {
        cb_free(full_path);
        cb_free(stripped_filename);
    }
}

static int compare_files_for_copy(const void* a, const void* b)
{
    const FileToCopy* fa = *(const FileToCopy**)a;
    const FileToCopy* fb = *(const FileToCopy**)b;

    int va = filename_parse_version(fa->full_path, NULL);
    int vb = filename_parse_version(fb->full_path, NULL);

    if (va == 0 && vb == 0)
        return 0;
    if (va == 0 && vb > 0)
        return -1;
    if (va > 0 && vb == 0)
        return 1;
    return va - vb;
}

void CB_FileCopyingScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_FileCopyingScene* scene = object;

    switch (scene->state)
    {
    case kFileCopyingStateInit:
    {
        cb_draw_logo_screen_and_display(CB_App->subheadFont, T(status_initializing));

        const char* sources[] = {".", "packed"};
        struct list_files_ud ud = {.scene = scene};

        for (size_t i = 0; i < sizeof(sources) / sizeof(const char*); ++i)
        {
            ud.directory = sources[i];
            playdate->file->listfiles(sources[i], collect_files_callback, &ud, true);
        }

        if (scene->files_to_copy->length > 1)
        {
            qsort(
                scene->files_to_copy->items, scene->files_to_copy->length, sizeof(void*),
                compare_files_for_copy
            );
        }

        if (scene->files_to_copy->length == 0)
        {
            scene->state = kFileCopyingStateDone;
        }
        else
        {
            scene->progress_max_width = cb_calculate_progress_max_width(
                CB_App->subheadFont, PROGRESS_STYLE_FRACTION, scene->files_to_copy->length
            );
            scene->state = kFileCopyingStateCopying;
        }
        break;
    }

    case kFileCopyingStateCopying:
    {
        if (scene->current_index < scene->files_to_copy->length)
        {
            FileToCopy* file = scene->files_to_copy->items[scene->current_index];

            char progress_message[32];
            snprintf(
                progress_message, sizeof(progress_message), "%d/%d", scene->current_index + 1,
                scene->files_to_copy->length
            );

            cb_draw_logo_screen_centered_split(
                CB_App->subheadFont, T(status_copying_files), progress_message,
                scene->progress_max_width
            );

            if (copy_one_file(file->full_path, file->filename))
            {
                json_value _true;
                _true.type = kJSONTrue;
                json_set_table_value(&scene->manifest, file->full_path, _true);
                scene->manifest_modified = true;
            }

            scene->current_index++;
        }
        else
        {
            scene->state = kFileCopyingStateDone;
        }
        break;
    }

    case kFileCopyingStateDone:
    {
        if (scene->manifest_modified)
        {
            write_json_to_disk(COPIED_FILES, scene->manifest);
        }
        CB_GameScanningScene* scanningScene = CB_GameScanningScene_new();
        CB_present(scanningScene->scene);
        break;
    }
    }
}

void CB_FileCopyingScene_free(void* object)
{
    CB_FileCopyingScene* scene = object;

    for (int i = 0; i < scene->files_to_copy->length; i++)
    {
        FileToCopy* file = scene->files_to_copy->items[i];
        cb_free(file->full_path);
        cb_free(file->filename);
        cb_free(file);
    }
    array_free(scene->files_to_copy);
    free_json_data(scene->manifest);
    CB_Scene_free(scene->scene);
    cb_free(scene);
}

CB_FileCopyingScene* CB_FileCopyingScene_new(void)
{
    CB_FileCopyingScene* scene = cb_calloc(1, sizeof(CB_FileCopyingScene));

    scene->scene = CB_Scene_new();
    scene->scene->id = "file-copying";
    scene->scene->managedObject = scene;
    scene->scene->update = CB_FileCopyingScene_update;
    scene->scene->free = CB_FileCopyingScene_free;
    scene->scene->use_user_stack = false;

    scene->files_to_copy = array_new();
    scene->current_index = 0;
    scene->state = kFileCopyingStateInit;
    scene->manifest_modified = false;

    parse_json(COPIED_FILES, &scene->manifest, kFileReadData | kFileRead);
    if (scene->manifest.type != kJSONTable)
    {
        free_json_data(scene->manifest);

        scene->manifest.type = kJSONTable;
        scene->manifest.data.tableval = cb_calloc(1, sizeof(JsonObject));
    }

    return scene;
}
