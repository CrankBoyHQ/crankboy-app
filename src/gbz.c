//
//  gbz.c
//  CrankBoy
//

#include "gbz.h"

#include "../libs/miniz/mini_gzip.h"

#include <string.h>

bool gbz_parse_header(GBZ_Header* out_header, const uint8_t* data, size_t size)
{
    if (size < GBZ_GZ_OFFSET)
        return false;

    const uint8_t* p = data;

    if (memcmp(p, GBZ_MAGIC, GBZ_MAGIC_LEN) != 0)
        return false;

    uint8_t version = p[8];
    if (version != 1)
        return false;

    for (size_t i = GBZ_FF_CHECK_START; i < GBZ_FF_CHECK_END; i++)
    {
        if (p[i] != 0xFF)
            return false;
    }

    out_header->version = version;
    out_header->is_gbc = p[9] != 0;
    out_header->crc32 = ((uint32_t)p[10] << 24) | ((uint32_t)p[11] << 16) | ((uint32_t)p[12] << 8) |
                        (uint32_t)p[13];

    // Decompressed size (little-endian uint32 at offset 14)
    out_header->original_size =
        (size_t)p[14] | ((size_t)p[15] << 8) | ((size_t)p[16] << 16) | ((size_t)p[17] << 24);

    memcpy(out_header->gb_header, p + 18, GBZ_ROM_HDR_SIZE);

    if (size == GBZ_GZ_OFFSET)
    {
        // Header-only file: no gz data
        out_header->gz_data = NULL;
        out_header->gz_size = 0;
    }
    else
    {
        out_header->gz_data = p + GBZ_GZ_OFFSET;
        out_header->gz_size = size - GBZ_GZ_OFFSET;

        // validate sizes match
        if (out_header->gz_size < 4)
            return false;

        const uint8_t* isize = out_header->gz_data + out_header->gz_size - 4;
        size_t gz_reported = (size_t)isize[0] | ((size_t)isize[1] << 8) | ((size_t)isize[2] << 16) |
                             ((size_t)isize[3] << 24);
        if (gz_reported != out_header->original_size)
            return false;
    }

    return true;
}

uint8_t gbz_read_header_byte(const GBZ_Header* header, uint16_t rom_address)
{
    if (rom_address < GBZ_ROM_HDR_START || rom_address > GBZ_ROM_HDR_END)
        return 0;
    return header->gb_header[rom_address - GBZ_ROM_HDR_START];
}

int gbz_decompress(const uint8_t* data, size_t size, uint8_t* out_buf, size_t out_max)
{
    GBZ_Header header;
    if (!gbz_parse_header(&header, data, size))
        return -1;

    if (header.original_size > out_max)
        return -2;

    struct mini_gzip gz;
    int status = mini_gz_start(&gz, (void*)header.gz_data, header.gz_size);
    if (status != 0)
        return -3;

    status = mini_gz_unpack(&gz, out_buf, header.original_size);
    if (status != (int)header.original_size)
        return -4;

    return (int)header.original_size;
}
