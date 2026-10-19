//  pdi.c
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
//  On-device reader for legacy "Playdate IMG" (.pdi) covers, used by the
//  startup migration that converts them into cvi containers.

#include "pdi.h"

#include "../libs/miniz/miniz.h"
#include "cvi.h"
#include "utility.h"

#include <string.h>

#define PDI_MAX_DIMENSION 1024
#define PDI_MAX_PAYLOAD (1u * 1024u * 1024u)
#define PDI_FIRST_GUESS 8192

static uint8_t* pdi_inflate(const uint8_t* src, size_t src_len, size_t* out_len)
{
    if (!src || src_len == 0)
        return NULL;

    mz_stream s;
    memset(&s, 0, sizeof(s));
    s.next_in = (const unsigned char*)src;
    s.avail_in = (unsigned int)src_len;
    if (mz_inflateInit(&s) != MZ_OK)
        return NULL;

    size_t cap = PDI_FIRST_GUESS;
    uint8_t* buf = cb_malloc(cap);
    if (!buf)
    {
        mz_inflateEnd(&s);
        return NULL;
    }
    size_t got = 0;
    bool done = false;

    while (!done)
    {
        if (s.avail_out == 0)
        {
            s.next_out = buf + got;
            s.avail_out = (unsigned int)(cap - got);
        }

        int status = mz_inflate(&s, MZ_NO_FLUSH);
        got = s.total_out;

        if (status == MZ_STREAM_END)
            done = true;
        else if (status != MZ_OK && status != MZ_BUF_ERROR)
            goto fail;

        if (!done)
        {
            // stalled with room left: won't terminate (truncated/corrupt)
            if (s.avail_out != 0)
                goto fail;

            if (cap >= PDI_MAX_PAYLOAD)
                goto fail;
            uint8_t* bigger = cb_realloc(buf, cap * 2);
            if (!bigger)
                goto fail;
            buf = bigger;
            cap *= 2;
        }
    }

    mz_inflateEnd(&s);
    *out_len = got;
    return buf;

fail:
    mz_inflateEnd(&s);
    cb_free(buf);
    return NULL;
}

// transparent pixels flatten to the white drawing surface
static uint8_t* pdi_build_gray8(
    const uint8_t* plane, uint32_t plane_stride, const uint8_t* opaque, uint32_t opaque_stride,
    uint32_t w, uint32_t h
)
{
    uint8_t* gray = cb_malloc(w * h);
    if (!gray)
        return NULL;

    for (uint32_t y = 0; y < h; ++y)
    {
        const uint8_t* prow = plane + y * plane_stride;
        const uint8_t* orow = opaque ? opaque + y * opaque_stride : NULL;
        uint8_t* g = gray + y * w;
        for (uint32_t x = 0; x < w; ++x)
        {
            bool white = (prow[x / 8] >> (7 - (x % 8))) & 1;
            if (orow && !((orow[x / 8] >> (7 - (x % 8))) & 1))
                white = true;
            g[x] = white ? 255 : 0;
        }
    }
    return gray;
}

// tight rows -> sdk-stride rows
static uint8_t* pdi_repack_plane(
    const uint8_t* src, uint32_t src_stride, uint32_t dst_stride, uint32_t w, uint32_t h
)
{
    uint32_t row_bytes = (w + 7) / 8;
    uint8_t* dst = cb_malloc(dst_stride * h);
    if (!dst)
        return NULL;
    memset(dst, 0, dst_stride * h);
    for (uint32_t y = 0; y < h; ++y)
        memcpy(dst + y * dst_stride, src + y * src_stride, row_bytes);
    return dst;
}

bool PDI_decode(const void* data_v, size_t len, PDIImage* img)
{
    const uint8_t* data = data_v;
    memset(img, 0, sizeof(*img));

    // header + magic
    if (len < sizeof(struct PDIHeader))
        return false;
    struct PDIHeader header;
    memcpy(&header, data, sizeof(header));
    if (memcmp(header.magic, PDI_MAGIC, 12) != 0)
        return false;
    bool compressed = (header.flags & PDI_FLAG_COMPRESSED) != 0;

    // caps bound the inflate buffer so corrupt transfers fail fast
    size_t plane_cap = (size_t)PDI_MAX_DIMENSION * (PDI_MAX_DIMENSION / 8 + 4) * 2;
    size_t payload_size = len - sizeof(struct PDIHeader);

    const uint8_t* payload = data + sizeof(struct PDIHeader);
    if (compressed)
    {
        // PDIMetadata sits between header and zlib payload
        if (payload_size < sizeof(struct PDIMetadata))
            return false;
        payload_size -= sizeof(struct PDIMetadata);
        payload += sizeof(struct PDIMetadata);
        if (payload_size > plane_cap)
            return false;
        payload = pdi_inflate(payload, payload_size, &payload_size);
        if (!payload)
            return false;
    }
    else if (payload_size > plane_cap)
    {
        return false;
    }

    bool ok = false;
    if (payload_size < 16)
        goto done;

    {
        struct PDICell cell;
        memcpy(&cell, payload, sizeof(cell));

        // all repo covers have zero insets and no transparency, but both
        // paths are honored anyway
        bool transparency = (cell.flags & PDI_CELL_FLAG_TRANSPARENCY) != 0;

        if (cell.clip_width == 0 || cell.clip_height == 0 || cell.clip_width > PDI_MAX_DIMENSION ||
            cell.clip_height > PDI_MAX_DIMENSION)
            goto done;
        if (cell.stride < (cell.clip_width + 7) / 8)
            goto done;

        size_t plane_len = (size_t)cell.stride * cell.clip_height;
        size_t needed = 16 + plane_len * (transparency ? 2 : 1);
        if (compressed)
            needed += (4 - (cell.stride % 4)) % 4;
        if (needed > payload_size)
            goto done;

        const uint8_t* white = payload + 16;
        const uint8_t* opaq = transparency ? white + plane_len : NULL;

        uint32_t out_stride = cvi_sdk_stride(cell.clip_width);
        img->width = cell.clip_width;
        img->height = cell.clip_height;
        img->plane =
            pdi_repack_plane(white, cell.stride, out_stride, cell.clip_width, cell.clip_height);
        if (!img->plane)
            goto done;
        img->gray8 = pdi_build_gray8(
            white, cell.stride, opaq, cell.stride, cell.clip_width, cell.clip_height
        );
        if (!img->gray8)
        {
            PDI_decode_free(img);
            goto done;
        }
        ok = true;
    }

done:
    if (compressed)
        cb_free((void*)payload);
    if (!ok)
        memset(img, 0, sizeof(*img));
    return ok;
}

void PDI_decode_free(PDIImage* img)
{
    cb_free(img->plane);
    cb_free(img->gray8);
    img->plane = NULL;
    img->gray8 = NULL;
}
