//
//  scenes/file_copying_scene.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//

#pragma once

#include "../scene.h"

typedef enum
{
    kFileCopyingStateInit,
    kFileCopyingStateCopying,
    kFileCopyingStateDone
} FileCopyingState;

typedef struct
{
    char* full_path;
    char* filename;
} FileToCopy;

typedef struct CB_FileCopyingScene
{
    CB_Scene* scene;
    CB_Array* files_to_copy;
    int current_index;
    FileCopyingState state;
    json_value manifest;
    bool manifest_modified;
    int progress_max_width;
} CB_FileCopyingScene;

CB_FileCopyingScene* CB_FileCopyingScene_new(void);
