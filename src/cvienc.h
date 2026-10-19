//
//  cvienc.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
//  On-device "CrankBoy CVI" encoder: builds cover containers straight from
//  an RGBA/gray buffer so the hub and the image conversion scene emit the same
//  format the downloader serves. Dither = Bayer 4x4 ordered at target size
//  (matches the repo pipeline's magick o4x4 pattern); compression = lz4
//  (default level, raw blocks - lz4hc would be too heavy on device).
//

#ifndef cvienc_h
#define cvienc_h

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    CVI_ENC_FIT_BOX = 0,      // scale to fit within value x value box
    CVI_ENC_HEIGHT_EXACT = 1  // height forced to value (no upscale)
} CVIEncFit;

typedef struct CVIEncRequest
{
    uint16_t value;  // box edge (FIT) or height (HEIGHT_EXACT)
    CVIEncFit fit;
} CVIEncRequest;

#define CVIENC_COVER_REQUEST_COUNT 2

extern const CVIEncRequest CVIENC_COVER_REQUESTS[CVIENC_COVER_REQUEST_COUNT];

typedef struct CVIEncSource
{
    const uint8_t* rgba;   // rgba8888; must be set unless gray8 is
    const uint8_t* gray8;  // width * height grayscale; used when rgba is NULL
    int width;
    int height;
    const uint8_t* passthrough_plane;
    const CVIEncRequest* requests;  // larger variant first
    int num_requests;
} CVIEncSource;

// builds container bytes into a cb_malloc'd buffer (caller frees via cb_free);
// variant requests that produce identical dims collapse (format needs >= 1)
void* CVIEnc_write(const CVIEncSource* src, size_t* out_size);

#endif /* cvienc_h */
