//
//  ft.h
//  CrankBoy
//
//  File Transfer Protocol (ft) interface
//

#ifndef ft_h
#define ft_h

#include <stdbool.h>

// Protocol constants
#define FT_CHUNK_SIZE 177                   // msg will be 256 chars (max possible)
#define FT_MAX_FILE_SIZE (8 * 1024 * 1024)  // 8MB max
#define FT_MAX_CHUNKS 65536                 // 2-digit hex limit (00-FF)
#define FT_WINDOW_SIZE 16                   // Pipelining window size (advertised in ready response)
#define FT_INITIAL_BATCH_SIZE 3             // Starting batch size for adaptive batching
#define FT_MAX_BATCH_SIZE 14  // Max batch size (leaves headroom for out-of-order chunks)

// Cleanup (called internally and on error)
void ft_cleanup(void);

// Protocol command handlers (called by serial.c)
bool ft_handle_begin(
    const char* filename, const char* size_str, const char* crc_str, const char* original_filename,
    const char* original_crc_str
);
bool ft_handle_chunk(const char* seq_str, const char* crc16_str, const char* base64_data);
bool ft_handle_end(const char* crc_str);
bool ft_handle_status(void);

#endif /* ft_h */
