//
//  cvi.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
//  Reader for the "CrankBoy CVI" cover container: one file per cover, one
//  lz4-compressed plane set per display size. Decompressed planes land
//  directly in pd->graphics->getBitmapData() layout (1bpp MSB-first,
//  bit 1 = white, rows padded to a multiple of 4 bytes; optional opaque
//  mask plane, bit 1 = opaque) right behind the pixel plane.
//

#ifndef cvi_h
#define cvi_h

#include <pd_api.h>
#include <stdint.h>

#define CVI_MAGIC "CrankBoy CVI"
#define CVI_VERSION 1
#define CVI_FLAG_LZ4 0x1
#define CVI_VARIANT_FLAG_HAS_MASK 0x1

// shared by reader and writer; keep in sync with cvi-spec.md
#define CVI_HEADER_SIZE (12 + 4 + 4 + 2)                    // 22
#define CVI_VARIANT_ENTRY_SIZE (4 + 2 + 2 + 2 + 2 + 4 + 4)  // 20

static inline uint32_t cvi_sdk_stride(uint32_t width)
{
    return ((width + 31) / 32) * 4;
}

typedef struct CB_CVIVariant
{
    uint32_t decompressed_size;  // color plane plus mask plane when present
    uint16_t width;
    uint16_t height;
    uint16_t stride;  // padded row bytes of the sdk layout
    uint16_t flags;
    uint32_t compressed_size;
    const uint8_t* block;  // lz4 block, borrowed from CB_CVI::data
} CB_CVIVariant;

typedef struct CB_CVI
{
    uint32_t flags;
    uint16_t num_variants;
    CB_CVIVariant* variants;
    uint8_t* data;  // raw file bytes, whole container
    uint32_t data_size;
} CB_CVI;

// reads + parses <path>, validating magic/version/table/blocks;
// CB_cvi_free must be called on success; on failure the loader cleans up
// itself (a post-failure CB_cvi_free is a harmless no-op)
bool CB_cvi_load(CB_CVI* cvi, const char* path);
void CB_cvi_free(CB_CVI* cvi);

// largest variant that fits into max_w x max_h, else the smallest variant
// (far cards and tiny displays end up sampling smaller art instead of
// upscaling a bigger variant)
const CB_CVIVariant* CVI_pick_variant(const CB_CVI* cvi, int max_w, int max_h);

// decompresses <variant> into a fresh LCDBitmap; io_buffer/size is a
// caller-owned scratch buffer reused across calls (grown on demand)
LCDBitmap* CVI_decode_variant(
    const CB_CVIVariant* variant, void** io_buffer, size_t* io_buffer_size
);

#endif /* cvi_h */
