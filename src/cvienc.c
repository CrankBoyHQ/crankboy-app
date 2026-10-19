//
//  cvienc.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
//  On-device cover container encoder. See cvienc.h for the contract.
//
//  Plane rules mirror the device reader (cvi.c) and the offline packer
//  (png2cvi.py): 1bpp MSB-first, bit 1 = white, rows padded so the stride
//  is a multiple of 4 bytes, no mask plane today. Dither = Bayer 4x4
//  ordered at target size so on-device converted art matches the magick
//  o4x4 look of the downloaded covers.
//

#include "cvienc.h"

#include "../libs/lz4/lz4.h"
#include "cvi.h"
#include "utility.h"

#include <string.h>

// order-4x4 (Bayer) threshold map, matches magick's o4x4 pattern
static const uint8_t cvienc_bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

const CVIEncRequest CVIENC_COVER_REQUESTS[CVIENC_COVER_REQUEST_COUNT] = {
    {240, CVI_ENC_FIT_BOX},
    {200, CVI_ENC_HEIGHT_EXACT},
};
// grayscale sample of the source with alpha flattened onto white
static uint8_t cvienc_gray_at(
    const uint8_t* rgba, const uint8_t* gray8, int src_w, int src_h, int sx, int sy
)
{
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    if (sx >= src_w)
        sx = src_w - 1;
    if (sy >= src_h)
        sy = src_h - 1;

    if (gray8)
        return gray8[sy * src_w + sx];

    const uint8_t* p = rgba + (sy * src_w + sx) * 4;
    if (p[3] < 32)
        return 255;

    // Rec709, matching the offline packer
    return (uint8_t)((p[0] * 213 + p[1] * 715 + p[2] * 72) / 1000);
}

// dithers one variant into the white plane (1bpp, sdk stride); returns
// cb_malloc'd plane bytes, len = stride * height
static uint8_t* cvienc_plane(
    const uint8_t* rgba, const uint8_t* gray8, int src_w, int src_h, int dst_w, int dst_h
)
{
    uint32_t stride = cvi_sdk_stride(dst_w);
    uint8_t* plane = cb_malloc(stride * dst_h);
    if (!plane)
        return NULL;
    memset(plane, 0, stride * dst_h);

    for (int ty = 0; ty < dst_h; ty++)
    {
        for (int tx = 0; tx < dst_w; tx++)
        {
            // gray8 sources are already-dithered 1bpp patterns (legacy
            // .pdi); interpolation smears their strokes, so nearest only
            int sx = tx * src_w / dst_w;
            int sy = ty * src_h / dst_h;
            uint8_t gray = cvienc_gray_at(rgba, gray8, src_w, src_h, sx, sy);
            uint8_t threshold =
                (uint8_t)(((int)cvienc_bayer4[(ty % 4) * 4 + (tx % 4)] + 1) * 255 / 16);

            // white pixels and the padding bit read as 0 in the white plane
            if (gray < threshold)
                continue;
            plane[ty * stride + tx / 8] |= 0x80u >> (tx % 8);
        }
    }
    return plane;
}

// packs one compressed block; returns cb_malloc'd bytes, len = compressed length
static uint8_t* cvienc_lz4(const uint8_t* src, uint32_t size, uint32_t* out_compressed)
{
    int bound = LZ4_compressBound((int)size);
    if (bound <= 0)
        return NULL;
    uint8_t* dst = cb_malloc(bound);
    if (!dst)
        return NULL;
    int written = LZ4_compress_default((const char*)src, (char*)dst, (int)size, bound);
    if (written <= 0)
    {
        cb_free(dst);
        return NULL;
    }
    uint8_t* out = cb_malloc(written);
    if (!out)
    {
        cb_free(dst);
        return NULL;
    }
    memcpy(out, dst, written);
    cb_free(dst);
    if (out_compressed)
        *out_compressed = written;
    return out;
}

void* CVIEnc_write(const CVIEncSource* src, size_t* out_size)
{
    if (!src || (!src->rgba && !src->gray8) || src->width <= 0 || src->height <= 0 ||
        !src->requests || src->num_requests <= 0)
        return NULL;

    // variant dims, deduped in emit order
    int dims[8][2];
    int n_dims = 0;
    if (src->num_requests > 8)
        return NULL;

    for (int r = 0; r < src->num_requests; ++r)
    {
        int dw, dh;
        const CVIEncRequest* req = &src->requests[r];
        if (req->fit == CVI_ENC_HEIGHT_EXACT)
        {
            // x-200 semantics: shrink-plus-only, so taller sources keep
            // their full aspect and shorter ones keep their height
            if (src->height <= req->value)
            {
                dw = src->width;
                dh = src->height;
            }
            else
            {
                dh = req->value;
                dw = (src->width * dh + src->height / 2) / src->height;
                if (dw < 1)
                    dw = 1;
            }
        }
        else
        {
            // fit-within, no upscale
            if (src->width <= req->value && src->height <= req->value)
            {
                dw = src->width;
                dh = src->height;
            }
            else
            {
                float scale = (float)req->value /
                              (float)(src->width > src->height ? src->width : src->height);
                dw = (int)(src->width * scale);
                dh = (int)(src->height * scale);
                if (dw < 1)
                    dw = 1;
                if (dh < 1)
                    dh = 1;
            }
        }

        bool duplicate = false;
        for (int i = 0; i < n_dims; ++i)
        {
            if (dims[i][0] == dw && dims[i][1] == dh)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        dims[n_dims][0] = dw;
        dims[n_dims][1] = dh;
        ++n_dims;
    }

    if (n_dims <= 0)
        return NULL;

    uint8_t* planes[8] = {0};
    uint8_t* comp[8] = {0};
    uint32_t comp_len[8] = {0};

    for (int i = 0; i < n_dims; ++i)
    {
        uint32_t plane_len = cvi_sdk_stride(dims[i][0]) * dims[i][1];
        if (src->passthrough_plane && dims[i][0] == src->width && dims[i][1] == src->height)
        {
            comp[i] = cvienc_lz4(src->passthrough_plane, plane_len, &comp_len[i]);
        }
        else
        {
            planes[i] = cvienc_plane(
                src->rgba, src->gray8, src->width, src->height, dims[i][0], dims[i][1]
            );
            if (!planes[i])
                goto fail;
            comp[i] = cvienc_lz4(planes[i], plane_len, &comp_len[i]);
        }
        if (!comp[i])
            goto fail;
    }

    {
        uint32_t table_size = CVI_HEADER_SIZE + n_dims * CVI_VARIANT_ENTRY_SIZE;
        uint32_t blocks_size = 0;
        for (int i = 0; i < n_dims; ++i)
            blocks_size += comp_len[i];

        uint8_t* out = cb_malloc(table_size + blocks_size);
        if (!out)
            goto fail;

        memcpy(out, CVI_MAGIC, 12);
        uint32_t version = CVI_VERSION;
        uint32_t flags = CVI_FLAG_LZ4;
        uint16_t num = n_dims;
        memcpy(out + 12, &version, 4);
        memcpy(out + 16, &flags, 4);
        memcpy(out + 20, &num, 2);

        for (int i = 0; i < n_dims; ++i)
        {
            uint8_t* entry = out + CVI_HEADER_SIZE + i * CVI_VARIANT_ENTRY_SIZE;
            uint32_t decompressed = cvi_sdk_stride(dims[i][0]) * dims[i][1];
            uint32_t offset = table_size;
            uint16_t vflags = 0;
            for (int j = 0; j < i; ++j)
                offset += comp_len[j];

            memcpy(entry + 0, &decompressed, 4);
            uint16_t w = dims[i][0];
            uint16_t h = dims[i][1];
            uint16_t stride = cvi_sdk_stride(dims[i][0]);
            memcpy(entry + 4, &w, 2);
            memcpy(entry + 6, &h, 2);
            memcpy(entry + 8, &stride, 2);
            memcpy(entry + 10, &vflags, 2);
            memcpy(entry + 12, &offset, 4);
            memcpy(entry + 16, &comp_len[i], 4);

            memcpy(out + offset, comp[i], comp_len[i]);
        }

        for (int i = 0; i < n_dims; ++i)
        {
            cb_free(comp[i]);
            cb_free(planes[i]);
        }
        if (out_size)
            *out_size = table_size + blocks_size;
        return out;
    }

fail:
    for (int i = 0; i < n_dims; ++i)
    {
        if (comp[i])
            cb_free(comp[i]);
        if (planes[i])
            cb_free(planes[i]);
    }
    return NULL;
}
