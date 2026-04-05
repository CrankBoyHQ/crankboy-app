//
//  ft.c
//  CrankBoy
//
//  File Transfer Protocol (ft) implementation
//

#include "ft.h"

#include "gbz.h"
#include "utility.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * File Transfer Protocol (ft - File Transfer) with Window-Based Pipelining
 * ============================================================================
 *
 * Protocol Specification:
 *
 * Host Commands (→ Device):
 *   ft:b:<filename>:<size>:<crc32>[:<orig_name>:<orig_crc>]  - Begin transfer
 *   ft:c:<seq>:<crc16>:<base64>                              - Send chunk
 *   ft:e:<crc32>                                             - End transfer
 *   ft:s                                                     - Query status
 *
 * Device Responses (← Host):
 *   ft:r:<code>            Ready (format: WWCC hex, WW=window, CC=chunk)
 *                           Example: ft:r:08B1 = window=8, chunk=177
 *   ft:a:<seq>             Cumulative ACK (chunks 0 through seq acknowledged)
 *   ft:n:<seq>:<code>      NACK with error code (immediate, not batched):
 *                           - "crc" = CRC mismatch (retry same chunk)
 *                           - "seq" = wrong sequence (resync to seq)
 *                           - "write" = write error (fatal)
 *                           - "size" = size exceeded (fatal)
 *   ft:d:<base>:<bitmap>   Status response: window_base + bitmap of received
 *                           chunks (bit 0 = window_base, bit 1 = window_base+1)
 *   ft:o:<filename>        OK - transfer complete (original name if decompressed)
 *   ft:x:<code>            Error with code:
 *                           - "busy" = transfer in progress
 *                           - "size" = invalid size
 *                           - "filename" = invalid filename
 *                           - "extension" = invalid extension
 *                           - "toobig" = file too big
 *                           - "write" = write error
 *                           - "crc" = CRC mismatch
 *                           - "nomem" = out of memory
 *                           - "gbz_header" = invalid GBZ header
 *                           - "decompress" = decompression failed
 *                           - "orig_crc" = original CRC mismatch
 *                           - "notransfer" = no transfer in progress
 *
 * Supported File Types:
 *   - .gb  - Game Boy ROMs (saved to games/)
 *   - .gbc - Game Boy Color ROMs (saved to games/)
 *   - .gbz - Compressed GB/GBC files (saved to games/, decompressed on device)
 *   - .pdi - Playdate cover images (saved to covers/ automatically)
 *
 * Target Directory Selection:
 *   The device automatically selects the target directory based on file extension:
 *   - .pdi files → covers/ directory
 *   - All other valid types (.gb, .gbc, .gbz) → games/ directory
 *
 * Temp File Naming:
 *   Temporary files during transfer: .ft_<filename>.tmp in target directory
 *   Backup of existing files: .ft_<filename>.bak in target directory
 *
 * Window-Based Pipelining with Adaptive Batching:
 *   - Device advertises window size in ft:r response (WW field)
 *   - Host can send up to <window_size> chunks without waiting for ACKs
 *   - Device buffers out-of-order chunks within the window
 *   - Device sends cumulative ACK for highest consecutive chunk received
 *   - Adaptive batching: starts at 3 chunks, increases to (window-2) after
 *     5 consecutive successful batches (no timeouts/NACKs)
 *   - On any timeout or NACK, batch size resets to 3
 *   - Host can query status (ft:s) to get bitmap of received chunks
 *   - Default window size: 16 (configurable via FT_WINDOW_SIZE define)
 */

// FT_CHUNK_SIZE, FT_MAX_FILE_SIZE, FT_MAX_CHUNKS, FT_WINDOW_SIZE are defined in ft.h

// Transfer states
typedef enum
{
    FT_STATE_IDLE,
    FT_STATE_RECEIVING,
} FtState;

// Chunk buffer entry for window-based pipelining
typedef struct
{
    uint8_t data[FT_CHUNK_SIZE];
    uint16_t length;
    uint32_t seq;
    bool valid;
    uint16_t crc16;
} FtChunkBuffer;

// Transfer context
static struct
{
    FtState state;
    char* filename;
    char* temp_path;
    char* final_path;
    char* bak_path;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t expected_crc;
    SDFile* file;
    char* original_filename;
    uint32_t original_crc;
    FtChunkBuffer chunk_buffer[FT_WINDOW_SIZE];
    uint32_t window_base;
    uint32_t last_ack_sent;
    uint8_t batch_size;
    uint8_t successful_batches;
} ft_ctx;

// Sanitize filename: minimal safety - only prevent path traversal
static bool sanitize_filename(const char* input, char* output, size_t output_size)
{
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_size - 1; i++)
    {
        char c = input[i];
        if (c == '/' || c == '\\')
        {
            continue;
        }
        output[j++] = c;
    }
    output[j] = '\0';

    if (j == 0)
    {
        return false;
    }

    if (strstr(output, "..") != NULL)
    {
        return false;
    }

    return true;
}

// Clean up transfer context
void ft_cleanup(void)
{
    if (ft_ctx.file)
    {
        playdate->file->close(ft_ctx.file);
        ft_ctx.file = NULL;
    }
    if (ft_ctx.temp_path)
    {
        playdate->file->unlink(ft_ctx.temp_path, 0);
        cb_free(ft_ctx.temp_path);
        ft_ctx.temp_path = NULL;
    }
    if (ft_ctx.filename)
    {
        cb_free(ft_ctx.filename);
        ft_ctx.filename = NULL;
    }
    if (ft_ctx.final_path)
    {
        cb_free(ft_ctx.final_path);
        ft_ctx.final_path = NULL;
    }
    if (ft_ctx.bak_path)
    {
        cb_free(ft_ctx.bak_path);
        ft_ctx.bak_path = NULL;
    }
    if (ft_ctx.original_filename)
    {
        cb_free(ft_ctx.original_filename);
        ft_ctx.original_filename = NULL;
    }
    ft_ctx.state = FT_STATE_IDLE;
    ft_ctx.expected_size = 0;
    ft_ctx.received_size = 0;
    ft_ctx.expected_crc = 0;
    ft_ctx.original_crc = 0;
    ft_ctx.window_base = 0;
    ft_ctx.last_ack_sent = 0;

    for (int i = 0; i < FT_WINDOW_SIZE; i++)
    {
        ft_ctx.chunk_buffer[i].valid = false;
    }

    ft_ctx.batch_size = FT_INITIAL_BATCH_SIZE;
    ft_ctx.successful_batches = 0;
}

// ============================================================================
// Window-based Pipelining Support
// ============================================================================

// Get buffer slot for a sequence number (returns -1 if out of window)
static int ft_get_buffer_slot(uint32_t seq)
{
    if (seq < ft_ctx.window_base || seq >= ft_ctx.window_base + FT_WINDOW_SIZE)
    {
        return -1;
    }
    return (int)(seq - ft_ctx.window_base);
}

// Store chunk in buffer
static bool ft_buffer_chunk(uint32_t seq, const uint8_t* data, uint16_t length, uint16_t crc16)
{
    int slot = ft_get_buffer_slot(seq);
    if (slot < 0)
    {
        return false;
    }

    FtChunkBuffer* buf = &ft_ctx.chunk_buffer[slot];
    memcpy(buf->data, data, length);
    buf->length = length;
    buf->seq = seq;
    buf->crc16 = crc16;
    buf->valid = true;
    return true;
}

// Flush consecutive chunks from window buffer to file
// Returns true if any chunks were written
static bool ft_flush_buffer(void)
{
    bool wrote_any = false;

    while (ft_ctx.received_size < ft_ctx.expected_size)
    {
        int slot = ft_get_buffer_slot(ft_ctx.window_base);
        if (slot < 0 || !ft_ctx.chunk_buffer[slot].valid)
        {
            break;
        }

        FtChunkBuffer* buf = &ft_ctx.chunk_buffer[slot];

        if (ft_ctx.received_size + buf->length > ft_ctx.expected_size)
        {
            serial_send_response("ft:n:%04X:size", ft_ctx.window_base);
            ft_cleanup();
            return false;
        }

        int written = playdate->file->write(ft_ctx.file, buf->data, buf->length);
        if (written != buf->length)
        {
            serial_send_response("ft:n:%04X:write", ft_ctx.window_base);
            ft_cleanup();
            return false;
        }

        ft_ctx.received_size += buf->length;
        buf->valid = false;
        ft_ctx.window_base++;
        wrote_any = true;
    }

    return wrote_any;
}

// Send cumulative ACK for highest consecutive chunk received
// Uses adaptive batching: starts at 3, increases to (window_size - 2) after 5 successful batches
static void ft_send_cumulative_ack(bool force, bool is_error)
{
    // window_base is the next chunk we expect / first unacknowledged chunk
    // So window_base - 1 is the highest consecutive chunk received and written
    uint32_t highest_acked = (ft_ctx.window_base > 0) ? ft_ctx.window_base - 1 : 0;

    // On error, reset adaptive batching to conservative values
    if (is_error)
    {
        ft_ctx.batch_size = FT_INITIAL_BATCH_SIZE;
        ft_ctx.successful_batches = 0;
    }

    // Only send ACK if:
    // 1. force is true, OR
    // 2. We've received new data since last ACK (window advanced), AND
    //    batch_size threshold reached
    bool should_ack = force;
    if (!should_ack && ft_ctx.window_base > ft_ctx.last_ack_sent)
    {
        uint32_t new_chunks = ft_ctx.window_base - ft_ctx.last_ack_sent;
        if (new_chunks >= ft_ctx.batch_size)
        {
            should_ack = true;
        }
    }

    if (should_ack && ft_ctx.window_base > ft_ctx.last_ack_sent)
    {
        serial_send_response("ft:a:%04X", highest_acked);
        ft_ctx.last_ack_sent = ft_ctx.window_base;

        if (!is_error)
        {
            ft_ctx.successful_batches++;
            if (ft_ctx.successful_batches >= 5 && ft_ctx.batch_size < FT_MAX_BATCH_SIZE)
            {
                ft_ctx.batch_size++;
                ft_ctx.successful_batches = 0;
            }
        }
    }
}

// Check if extension is valid (.gb, .gbc, .gbz, .pdi)
static bool is_valid_extension(const char* filename)
{
    const char* ext = get_extension(filename);
    if (!ext)
        return false;
    return (
        strcasecmp(ext, ".gb") == 0 || strcasecmp(ext, ".gbc") == 0 ||
        strcasecmp(ext, ".gbz") == 0 || strcasecmp(ext, ".pdi") == 0
    );
}

// Handle ft:b command - Begin transfer
// Format: ft:b:<filename>:<size>:<crc32>[:<original_filename>:<original_crc>]
bool ft_handle_begin(
    const char* filename, const char* size_str, const char* crc_str, const char* original_filename,
    const char* original_crc_str
)
{
    // Ensure clean state - cleanup any stale transfer
    if (ft_ctx.state != FT_STATE_IDLE)
    {
        ft_cleanup();
    }

    if (ft_ctx.state != FT_STATE_IDLE)
    {
        serial_send_response("ft:x:busy");
        return false;
    }

    uint32_t size = (uint32_t)strtoul(size_str, NULL, 10);
    if (size == 0 || size > FT_MAX_FILE_SIZE)
    {
        serial_send_response("ft:x:size");
        return false;
    }

    uint32_t crc = (uint32_t)strtoul(crc_str, NULL, 16);

    char decoded_name[512];
    if (url_decode(filename, decoded_name, sizeof(decoded_name)) < 0)
    {
        serial_send_response("ft:x:filename");
        return false;
    }

    char safe_name[256];
    if (!sanitize_filename(decoded_name, safe_name, sizeof(safe_name)))
    {
        serial_send_response("ft:x:filename");
        return false;
    }

    if (!is_valid_extension(safe_name))
    {
        serial_send_response("ft:x:extension");
        return false;
    }

    uint32_t num_chunks = (size + FT_CHUNK_SIZE - 1) / FT_CHUNK_SIZE;
    if (num_chunks > FT_MAX_CHUNKS)
    {
        serial_send_response("ft:x:toobig");
        return false;
    }

    const char* ext = get_extension(safe_name);
    bool is_cover = (ext && strcasecmp(ext, ".pdi") == 0);
    const char* target_dir =
        is_cover ? cb_gb_directory_path(CB_coversPath) : cb_gb_directory_path(CB_gamesPath);

    ft_ctx.filename = cb_strdup(safe_name);
    if (!ft_ctx.filename)
    {
        serial_send_response("ft:x:nomem");
        ft_cleanup();
        return false;
    }

    ft_ctx.final_path = aprintf("%s/%s", target_dir, safe_name);
    if (!ft_ctx.final_path)
    {
        serial_send_response("ft:x:nomem");
        ft_cleanup();
        return false;
    }

    ft_ctx.temp_path = aprintf("%s/.ft_%s.tmp", target_dir, safe_name);
    if (!ft_ctx.temp_path)
    {
        serial_send_response("ft:x:nomem");
        ft_cleanup();
        return false;
    }

    ft_ctx.bak_path = aprintf("%s/.ft_%s.bak", target_dir, safe_name);
    if (!ft_ctx.bak_path)
    {
        serial_send_response("ft:x:nomem");
        ft_cleanup();
        return false;
    }

    ft_ctx.expected_size = size;
    ft_ctx.expected_crc = crc;
    ft_ctx.received_size = 0;
    ft_ctx.window_base = 0;
    ft_ctx.last_ack_sent = 0;

    for (int i = 0; i < FT_WINDOW_SIZE; i++)
    {
        ft_ctx.chunk_buffer[i].valid = false;
    }

    ft_ctx.batch_size = FT_INITIAL_BATCH_SIZE;
    ft_ctx.successful_batches = 0;

    if (original_filename && original_crc_str)
    {
        char decoded_orig_name[512];
        if (url_decode(original_filename, decoded_orig_name, sizeof(decoded_orig_name)) < 0)
        {
            serial_send_response("ft:x:filename");
            return false;
        }

        char safe_orig_name[256];
        if (!sanitize_filename(decoded_orig_name, safe_orig_name, sizeof(safe_orig_name)))
        {
            serial_send_response("ft:x:filename");
            return false;
        }

        ft_ctx.original_filename = cb_strdup(safe_orig_name);
        if (!ft_ctx.original_filename)
        {
            serial_send_response("ft:x:nomem");
            ft_cleanup();
            return false;
        }
        ft_ctx.original_crc = (uint32_t)strtoul(original_crc_str, NULL, 16);
    }
    else
    {
        ft_ctx.original_filename = NULL;
        ft_ctx.original_crc = 0;
    }

    ft_ctx.file = playdate->file->open(ft_ctx.temp_path, kFileWrite);
    if (!ft_ctx.file)
    {
        serial_send_response("ft:x:write");
        ft_cleanup();
        return false;
    }

    ft_ctx.state = FT_STATE_RECEIVING;
    // Send ready with window size (high byte) and chunk size (low byte) in 4-digit hex
    // Format: WWCC where WW = window size (2 hex digits), CC = chunk size (2 hex digits)
    uint16_t ready_code = (FT_WINDOW_SIZE << 8) | FT_CHUNK_SIZE;
    serial_send_response("ft:r:%04X", ready_code);
    return true;
}

// Handle ft:c command - Send chunk with window-based pipelining
// Format: ft:c:<seq>:<crc16>:<base64> (seq = 4-digit hex, crc16 = lower 16 bits of CRC32)
bool ft_handle_chunk(const char* seq_str, const char* crc16_str, const char* base64_data)
{
    if (ft_ctx.state != FT_STATE_RECEIVING)
    {
        serial_send_response("ft:x:notransfer");
        return false;
    }

    uint32_t seq = (uint32_t)strtoul(seq_str, NULL, 16);

    uint16_t expected_crc16 = (uint16_t)strtoul(crc16_str, NULL, 16);

    uint8_t decoded[FT_CHUNK_SIZE];
    int decoded_len = base64_decode(base64_data, strlen(base64_data), decoded, sizeof(decoded));
    if (decoded_len < 0)
    {
        serial_send_response("ft:n:%04X:crc", seq);
        return false;
    }

    uint32_t full_crc = crc32_for_buffer(decoded, decoded_len);
    uint16_t actual_crc16 = (uint16_t)(full_crc & 0xFFFF);
    if (actual_crc16 != expected_crc16)
    {
        // Immediate NACK on CRC mismatch
        serial_send_response("ft:n:%04X:crc", seq);
        return false;
    }

    if (seq < ft_ctx.window_base)
    {
        // Duplicate chunk - already processed, re-send ACK (not an error)
        ft_send_cumulative_ack(true, false);
        return true;
    }

    // Check if chunk is within receive window
    if (seq >= ft_ctx.window_base + FT_WINDOW_SIZE)
    {
        // Chunk is ahead of window - send NACK to request resync
        serial_send_response("ft:n:%04X:seq", ft_ctx.window_base);
        return false;
    }

    if (!ft_buffer_chunk(seq, decoded, decoded_len, expected_crc16))
    {
        serial_send_response("ft:n:%04X:seq", ft_ctx.window_base);
        return false;
    }

    ft_flush_buffer();

    // Send cumulative ACK (with adaptive batching)
    // Only force ACK for first chunk (seq 0) - not an error
    bool force_ack = (seq == 0 && ft_ctx.window_base == 1);
    ft_send_cumulative_ack(force_ack, false);

    return true;
}

// Handle ft:e command - End transfer
// Format: ft:e:<crc32>
bool ft_handle_end(const char* crc_str)
{
    if (ft_ctx.state != FT_STATE_RECEIVING)
    {
        serial_send_response("ft:x:notransfer");
        return false;
    }

    playdate->file->close(ft_ctx.file);
    ft_ctx.file = NULL;

    if (ft_ctx.received_size != ft_ctx.expected_size)
    {
        serial_send_response("ft:x:size");
        ft_cleanup();
        return false;
    }

    uint32_t actual_crc;
    if (cb_calculate_crc32(ft_ctx.temp_path, kFileReadData, &actual_crc))
    {
        if (actual_crc != ft_ctx.expected_crc)
        {
            serial_send_response("ft:x:crc");
            ft_cleanup();
            return false;
        }
    }

    // Check if this is a GBZ file that needs decompression
    const char* ext = get_extension(ft_ctx.filename);
    bool is_gbz = ext && strcasecmp(ext, ".gbz") == 0;

    if (is_gbz && ft_ctx.original_filename)
    {
        SDFile* gbz_file = playdate->file->open(ft_ctx.temp_path, kFileReadData);
        if (!gbz_file)
        {
            serial_send_response("ft:x:read");
            ft_cleanup();
            return false;
        }

        playdate->file->seek(gbz_file, 0, SEEK_END);
        int gbz_size = playdate->file->tell(gbz_file);
        playdate->file->seek(gbz_file, 0, SEEK_SET);

        uint8_t* gbz_data = cb_malloc(gbz_size);
        if (!gbz_data)
        {
            playdate->file->close(gbz_file);
            serial_send_response("ft:x:nomem");
            ft_cleanup();
            return false;
        }

        int bytes_read = playdate->file->read(gbz_file, gbz_data, gbz_size);
        playdate->file->close(gbz_file);

        if (bytes_read != gbz_size)
        {
            cb_free(gbz_data);
            serial_send_response("ft:x:read");
            ft_cleanup();
            return false;
        }

        GBZ_Header header;
        if (!gbz_parse_header(&header, gbz_data, gbz_size))
        {
            cb_free(gbz_data);
            serial_send_response("ft:x:gbz_header");
            ft_cleanup();
            return false;
        }

        uint8_t* decompressed = cb_malloc(header.original_size);
        if (!decompressed)
        {
            cb_free(gbz_data);
            serial_send_response("ft:x:nomem");
            ft_cleanup();
            return false;
        }

        int decompressed_size =
            gbz_decompress(gbz_data, gbz_size, decompressed, header.original_size);
        cb_free(gbz_data);

        if (decompressed_size != (int)header.original_size)
        {
            cb_free(decompressed);
            serial_send_response("ft:x:decompress");
            ft_cleanup();
            return false;
        }

        uint32_t decompressed_crc = crc32_for_buffer(decompressed, decompressed_size);
        if (decompressed_crc != ft_ctx.original_crc)
        {
            cb_free(decompressed);
            serial_send_response("ft:x:orig_crc");
            ft_cleanup();
            return false;
        }

        const char* games_dir = cb_gb_directory_path(CB_gamesPath);
        char* original_path = aprintf("%s/%s", games_dir, ft_ctx.original_filename);

        char* orig_bak_path = aprintf("%s/.ft_%s.bak", games_dir, ft_ctx.original_filename);
        if (cb_file_exists(original_path, kFileReadData))
        {
            playdate->file->unlink(orig_bak_path, 0);
            playdate->file->rename(original_path, orig_bak_path);
        }

        SDFile* out_file = playdate->file->open(original_path, kFileWrite);
        if (!out_file)
        {
            cb_free(decompressed);
            cb_free(original_path);
            cb_free(orig_bak_path);
            serial_send_response("ft:x:write");
            ft_cleanup();
            return false;
        }

        int written = playdate->file->write(out_file, decompressed, decompressed_size);
        playdate->file->close(out_file);
        cb_free(decompressed);

        if (written != decompressed_size)
        {
            if (cb_file_exists(orig_bak_path, kFileReadData))
            {
                playdate->file->rename(orig_bak_path, original_path);
            }
            cb_free(original_path);
            cb_free(orig_bak_path);
            serial_send_response("ft:x:write");
            ft_cleanup();
            return false;
        }

        playdate->file->unlink(orig_bak_path, 0);
        playdate->file->unlink(ft_ctx.temp_path, 0);

        cb_free(original_path);
        cb_free(orig_bak_path);

        serial_send_response("ft:o:%s", ft_ctx.original_filename);
    }
    else
    {
        if (cb_file_exists(ft_ctx.final_path, kFileReadData))
        {
            playdate->file->unlink(ft_ctx.bak_path, 0);
            playdate->file->rename(ft_ctx.final_path, ft_ctx.bak_path);
        }

        if (playdate->file->rename(ft_ctx.temp_path, ft_ctx.final_path) != 0)
        {
            if (cb_file_exists(ft_ctx.bak_path, kFileReadData))
            {
                playdate->file->rename(ft_ctx.bak_path, ft_ctx.final_path);
            }
            serial_send_response("ft:x:write");
            ft_cleanup();
            return false;
        }

        playdate->file->unlink(ft_ctx.bak_path, 0);

        serial_send_response("ft:o:%s", ft_ctx.filename);
    }

    ft_cleanup();
    return true;
}

// Handle ft:s command - Query status
// Format: ft:s
// Response: ft:d:<window_base>:<bitmap_hex>  - bitmap shows which chunks in window are received
//   Each bit represents a chunk: bit 0 = window_base, bit 1 = window_base+1, etc.
//   Example: window_base=0, bitmap=0F means chunks 0,1,2,3 received
bool ft_handle_status(void)
{
    if (ft_ctx.state == FT_STATE_RECEIVING)
    {
        uint16_t bitmap = 0;
        for (int i = 0; i < FT_WINDOW_SIZE; i++)
        {
            if (ft_ctx.chunk_buffer[i].valid)
            {
                bitmap |= (1 << i);
            }
        }
        serial_send_response("ft:d:%04X:%04X", ft_ctx.window_base, bitmap);
    }
    else
    {
        serial_send_response("ft:d:0000:0000");
    }
    return true;
}
