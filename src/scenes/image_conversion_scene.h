#pragma once

#include "../scene.h"

#include <stdio.h>

// converts png to pdi. Returns NULL on failure.
// if either max_width or max_height non-zero, scales down image if it exceeds that size
void* png_to_pdi(
    const char* context_fname, const void* png_data, int png_size, size_t* out_size, int max_width,
    int max_height
);

unsigned char* cb_decode_png(
    const char* context_fname, const void* png_data, int png_size, int* out_width, int* out_height
);
void cb_free_decoded_image(unsigned char* img_data);
void* rgba_to_pdi(
    const char* context_fname, unsigned char* img_data, int width, int height, size_t* out_size,
    int max_width, int max_height
);

/* Converts PNG cover art into PDI, then launches LibraryScene */

typedef enum
{
    kStateListingFiles,
    kStateConverting,
    kStateDone
} ConversionState;

typedef struct CB_ImageConversionScene
{
    CB_Scene* scene;
    unsigned idx;
    char** files;
    size_t files_count;
    int progress_max_width;
    ConversionState state;
} CB_ImageConversionScene;

CB_ImageConversionScene* CB_ImageConversionScene_new(void);

// returns true for .png, .jpg, .jpeg, .bmp
bool filename_has_stbi_extension(const char* fname);
