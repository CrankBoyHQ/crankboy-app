//  Compressed Game Boy ROM format (.gbz)
//
//  File layout:
//    [0:8]   Magic: CB 00 FF 47 42 67 7A
//    [8]     Compression scheme version (currently 1)
//    [9]     1 if original file was .gbc, 0 if .gb
//    [10:14] CRC32 of original ROM (big-endian)
//    [14:18] Decompressed ROM size in bytes (little-endian uint32)
//    [18:46] ROM header bytes 0x134–0x14F (title + cartridge info, no Nintendo logo)
//    [46:0x150] 0xFF padding (bytes 0x100–0x14F are validated on parse)
//    [0x150:]   gzip-compressed ROM data (absent if file is exactly 0x150 bytes)
//

#ifndef gbz_h
#define gbz_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBZ_MAGIC "CB\x00\xFFGBgz"
#define GBZ_MAGIC_LEN 8
#define GBZ_ROM_HDR_START 0x134
#define GBZ_ROM_HDR_END 0x14F
#define GBZ_ROM_HDR_SIZE (GBZ_ROM_HDR_END - GBZ_ROM_HDR_START + 1) /* 28 bytes */
#define GBZ_FF_CHECK_START 0x100
#define GBZ_FF_CHECK_END 0x150 /* exclusive */
#define GBZ_GZ_OFFSET 0x150

typedef struct
{
    uint8_t version;
    bool is_gbc;
    uint32_t crc32;

    // ROM bytes 0x134–0x14F
    uint8_t gb_header[GBZ_ROM_HDR_SIZE];

    // decompressed ROM size (from file header field)
    size_t original_size;

    // pointer into source buffer, not owned
    const uint8_t* gz_data;
    size_t gz_size;
} GBZ_Header;

// Parse the gbz header from memory.
// Returns false if the data is too short, magic is wrong, or version is unsupported.
bool gbz_parse_header(GBZ_Header* o_gbz, const uint8_t* data, size_t size);

// Return the byte at rom_address from the embedded ROM header blob,
// if rom_address falls within 0x134–0x14F. Otherwise, returns 0.
uint8_t gbz_read_header_byte(const GBZ_Header* header, uint16_t rom_address);

// Decompress the gbz ROM data into out_buf (capacity out_max).
// Returns the number of bytes written, or negative on failure.
int gbz_decompress(const uint8_t* data, size_t size, uint8_t* out_buf, size_t out_max);

#endif /* gbz_h */
