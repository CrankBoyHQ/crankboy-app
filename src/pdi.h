#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// format: cranksters/playdate-reverse-engineering formats/pdi.md;

#define PDI_MAGIC "Playdate IMG"
#define PDI_FLAG_COMPRESSED 0x80000000
#define PDI_CELL_FLAG_TRANSPARENCY 3

struct PDIHeader
{
    char magic[12];
    uint32_t flags;
};

struct PDIMetadata
{
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
};

struct PDICell
{
    uint16_t clip_width;
    uint16_t clip_height;
    uint16_t stride;
    uint16_t clip_left;
    uint16_t clip_right;
    uint16_t clip_top;
    uint16_t clip_bottom;
    uint16_t flags;
};

_Static_assert(sizeof(struct PDIHeader) == 16, "");
_Static_assert(offsetof(struct PDIHeader, flags) == 12, "");
_Static_assert(sizeof(struct PDIMetadata) == 16, "");
_Static_assert(sizeof(struct PDICell) == 16, "");
_Static_assert(offsetof(struct PDICell, stride) == 4, "");
_Static_assert(offsetof(struct PDICell, flags) == 14, "");

// decoded .pdi content
typedef struct PDIImage
{
    uint32_t width;
    uint32_t height;
    uint8_t* plane;
    uint8_t* gray8;
} PDIImage;

bool PDI_decode(const void* data, size_t len, PDIImage* img);
void PDI_decode_free(PDIImage* img);
