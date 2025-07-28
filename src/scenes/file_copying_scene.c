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

struct list_files_ud
{
    CB_FileCopyingScene* scene;
    const char* directory;
};

static bool copy_one_file(const char* full_path, const char* filename)
{
    const char* extension = strrchr((char*)filename, '.');

    char* dst_path = NULL;

    if (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".jpg") ||
        !strcasecmp(extension, ".jpeg") || !strcasecmp(extension, ".bmp") ||
        !strcasecmp(extension, ".pdi"))
    {
        dst_path = aprintf("%s/%s", CB_coversPath, filename);
    }
    else if (!strcasecmp(extension, ".gb") || !strcasecmp(extension, ".gbc"))
    {
        dst_path = aprintf("%s/%s", CB_gamesPath, filename);
    }
    else if (!strcasecmp(extension, ".sav"))
    {
        dst_path = aprintf("%s/%s", CB_savesPath, filename);
    }
    else if (!strcasecmp(extension, ".state"))
    {
        dst_path = aprintf("%s/%s", CB_statesPath, filename);
    }
    else if (!strcmp(filename, "dmg_boot.bin"))
    {
        dst_path = aprintf("./%s", filename);
    }

    if (!dst_path)
    {
        return false;
    }

    size_t size;
    void* dat = cb_read_entire_file(full_path, &size, kFileRead);

    bool success = false;
    if (dat && size > 0)
    {
        success = cb_write_entire_file(dst_path, dat, size);
        if (!success)
        {
            playdate->system->logToConsole("Error: Failed to write to %s", dst_path);
        }
        cb_free(dat);
    }
    else
    {
        playdate->system->logToConsole("Error: Failed to read from %s", full_path);
    }

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

    const char* extension = strrchr((char*)filename, '.');
    bool should_copy = false;

    if (!strcmp(filename, "dmg_boot.bin"))
    {
        should_copy = true;
    }
    else if (extension)
    {
        if (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".jpg") ||
            !strcasecmp(extension, ".jpeg") || !strcasecmp(extension, ".bmp") ||
            !strcasecmp(extension, ".pdi") || !strcasecmp(extension, ".gb") ||
            !strcasecmp(extension, ".gbc") || !strcasecmp(extension, ".sav") ||
            !strcasecmp(extension, ".state"))
        {
            should_copy = true;
        }
    }

    if (should_copy)
    {
        FileToCopy* file_to_copy = cb_malloc(sizeof(FileToCopy));
        file_to_copy->full_path = full_path;
        file_to_copy->filename = cb_strdup(filename);
        array_push(scene->files_to_copy, file_to_copy);
    }
    else
    {
        cb_free(full_path);
    }
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
        cb_draw_logo_screen_and_display(CB_App->subheadFont, "Initializing...");

        const char* sources[] = {"."};
        struct list_files_ud ud = {.scene = scene};

        for (size_t i = 0; i < sizeof(sources) / sizeof(const char*); ++i)
        {
            ud.directory = sources[i];
            playdate->file->listfiles(sources[i], collect_files_callback, &ud, true);
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
                CB_App->subheadFont, "Copying Files... ", progress_message,
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
    cb_free(scene);
}

CB_FileCopyingScene* CB_FileCopyingScene_new(void)
{
    CB_FileCopyingScene* scene = cb_calloc(1, sizeof(CB_FileCopyingScene));

    scene->scene = CB_Scene_new();
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
