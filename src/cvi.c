//
//  cvi.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
//  Reader for the "CrankBoy CVI" cover container. The byte layout is
//  documented in the covers repo (cvi-spec.md); the structs in cvi.h
//  mirror it.
//

#include "cvi.h"

#include "../libs/lz4/lz4.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>

bool CB_cvi_load(CB_CVI* cvi, const char* path)
{
    memset(cvi, 0, sizeof(*cvi));
    size_t file_size = 0;
    uint8_t* content = (uint8_t*)cb_read_entire_file(path, &file_size, kFileReadData | kFileRead);
    if (!content || file_size < CVI_HEADER_SIZE)
    {
        cb_free(content);
        return false;
    }

    char magic[12];
    memcpy(magic, content, sizeof(magic));
    if (memcmp(magic, CVI_MAGIC, sizeof(magic)) != 0)
    {
        cb_free(content);
        return false;
    }

    uint32_t version, flags;
    uint16_t num_variants;
    memcpy(&version, content + 12, sizeof(version));
    memcpy(&flags, content + 16, sizeof(flags));
    memcpy(&num_variants, content + 20, sizeof(num_variants));

    if (version != CVI_VERSION || !(flags & CVI_FLAG_LZ4))
    {
        cb_free(content);
        return false;
    }

    if (num_variants == 0 ||
        (size_t)CVI_HEADER_SIZE + (size_t)num_variants * CVI_VARIANT_ENTRY_SIZE > file_size)
    {
        cb_free(content);
        return false;
    }

    cvi->variants = (CB_CVIVariant*)cb_malloc(sizeof(CB_CVIVariant) * num_variants);
    if (!cvi->variants)
    {
        cb_free(content);
        return false;
    }
    cvi->num_variants = num_variants;
    cvi->flags = flags;
    cvi->data = (uint8_t*)content;
    cvi->data_size = file_size;

    uint32_t block_end = CVI_HEADER_SIZE + (uint32_t)num_variants * CVI_VARIANT_ENTRY_SIZE;
    for (uint16_t i = 0; i < num_variants; i++)
    {
        const uint8_t* entry = content + CVI_HEADER_SIZE + i * CVI_VARIANT_ENTRY_SIZE;
        CB_CVIVariant* v = &cvi->variants[i];
        uint32_t offset;
        memcpy(&v->decompressed_size, entry + 0, sizeof(uint32_t));
        memcpy(&v->width, entry + 4, sizeof(uint16_t));
        memcpy(&v->height, entry + 6, sizeof(uint16_t));
        memcpy(&v->stride, entry + 8, sizeof(uint16_t));
        memcpy(&v->flags, entry + 10, sizeof(uint16_t));
        memcpy(&offset, entry + 12, sizeof(uint32_t));
        memcpy(&v->compressed_size, entry + 16, sizeof(uint32_t));

        // fail fast on layout deviations instead of feeding garbage to lz4
        uint32_t rows = v->stride * v->height;
        if (v->flags & CVI_VARIANT_FLAG_HAS_MASK)
            rows *= 2;
        if (v->stride != cvi_sdk_stride(v->width) || rows != v->decompressed_size ||
            v->compressed_size == 0 || offset < block_end ||
            (uint64_t)offset + v->compressed_size > file_size)
        {
            CB_cvi_free(cvi);
            return false;
        }
        v->block = cvi->data + offset;
    }

    // variants may appear in any order; blocks must fill the space between
    // table end and EOF without overlapping each other
    for (uint16_t i = 0; i < num_variants; i++)
    {
        for (uint16_t j = (uint16_t)(i + 1); j < num_variants; j++)
        {
            const CB_CVIVariant* a = &cvi->variants[i];
            const CB_CVIVariant* b = &cvi->variants[j];
            if (a->block < b->block + b->compressed_size &&
                b->block < a->block + a->compressed_size)
            {
                CB_cvi_free(cvi);
                return false;
            }
        }
    }

    return true;
}

void CB_cvi_free(CB_CVI* cvi)
{
    if (cvi->variants)
        cb_free(cvi->variants);
    if (cvi->data)
        cb_free(cvi->data);
    memset(cvi, 0, sizeof(*cvi));
}

const CB_CVIVariant* CVI_pick_variant(const CB_CVI* cvi, int max_w, int max_h)
{
    const CB_CVIVariant* best = NULL;
    uint32_t best_pixels = 0;
    for (uint16_t i = 0; i < cvi->num_variants; i++)
    {
        const CB_CVIVariant* v = &cvi->variants[i];
        if (v->width <= (uint16_t)max_w && v->height <= (uint16_t)max_h &&
            (uint32_t)v->width * v->height > best_pixels)
        {
            best = v;
            best_pixels = (uint32_t)v->width * v->height;
        }
    }
    if (best)
        return best;

    // nothing fits: sample the smallest variant, down-scaling at draw time
    const CB_CVIVariant* smallest = &cvi->variants[0];
    for (uint16_t i = 1; i < cvi->num_variants; i++)
    {
        const CB_CVIVariant* v = &cvi->variants[i];
        if ((uint32_t)v->width * v->height < (uint32_t)smallest->width * smallest->height)
            smallest = v;
    }
    return smallest;
}

LCDBitmap* CVI_decode_variant(
    const CB_CVIVariant* variant, void** io_buffer, size_t* io_buffer_size
)
{
    uint32_t plane_bytes = variant->stride * variant->height;
    uint32_t decompressed_size = variant->decompressed_size;
    if (!io_buffer || !io_buffer_size)
        return NULL;

    if (*io_buffer_size < decompressed_size)
    {
        void* new_buffer = cb_realloc(*io_buffer, decompressed_size);
        if (!new_buffer)
            return NULL;
        *io_buffer = new_buffer;
        *io_buffer_size = decompressed_size;
    }
    uint8_t* scratch = (uint8_t*)*io_buffer;
    if (!scratch)
        return NULL;

    int n = LZ4_decompress_safe(
        (const char*)variant->block, (char*)scratch, variant->compressed_size,
        (int)decompressed_size
    );
    if (n != (int)decompressed_size)
    {
        uint32_t block_hash = 2166136261u;
        for (uint32_t i = 0; i < variant->compressed_size; ++i)
        {
            block_hash ^= variant->block[i];
            block_hash *= 16777619u;
        }
        playdate->system->logToConsole(
            "cvi: lz4 need=%u got=%d (comp %u, w%u h%u, block hash=%08x)", decompressed_size, n,
            variant->compressed_size, variant->width, variant->height, block_hash
        );
        playdate->system->logToConsole(
            "cvi: lz4 block head=%02x%02x%02x%02x", variant->block[0], variant->block[1],
            variant->block[2], variant->block[3]
        );
        return NULL;
    }

    LCDBitmap* bitmap = playdate->graphics->newBitmap(
        variant->width, variant->height,
        (variant->flags & CVI_VARIANT_FLAG_HAS_MASK) ? kColorClear : kColorWhite
    );
    if (!bitmap)
    {
        playdate->system->logToConsole(
            "cvi: newBitmap %ux%u failed", variant->width, variant->height
        );
        return NULL;
    }

    int rowbytes;
    uint8_t *pixel_data, *mask_data;
    playdate->graphics->getBitmapData(bitmap, NULL, NULL, &rowbytes, &mask_data, &pixel_data);
    size_t copy_bytes =
        (size_t)variant->stride < (size_t)rowbytes ? (size_t)variant->stride : (size_t)rowbytes;

    const uint8_t* src = scratch;
    uint8_t* dst = pixel_data;
    for (int y = 0; y < variant->height; ++y)
    {
        memcpy(dst, src, copy_bytes);
        src += variant->stride;
        dst += rowbytes;
    }
    if ((variant->flags & CVI_VARIANT_FLAG_HAS_MASK) && mask_data)
    {
        dst = mask_data;
        for (int y = 0; y < variant->height; ++y)
        {
            memcpy(dst, src, copy_bytes);
            src += variant->stride;
            dst += rowbytes;
        }
    }

    return bitmap;
}
