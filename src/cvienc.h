//
//  cvienc.h
//  CrankBoy
//
//  Maintained and developed by the CrankBoy dev team.
//
//  On-device "CrankBoy CVI" encoder: builds cover containers straight from
//  an RGBA buffer so the hub and the PNG converter scene emit the same
//  format the downloader serves. Dither = Bayer 4x4 ordered at target size
//  (matches the repo pipeline's magick o4x4 pattern); compression = lz4
//  (default level, raw blocks — device never needs lz4hc).
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

typedef struct CVIEncSource
{
    const uint8_t* rgba;  // rgba8888
    int width;
    int height;
    const CVIEncRequest* requests;  // larger variant first
    int num_requests;
} CVIEncSource;

// builds container bytes into a cb_malloc'd buffer (caller frees via cb_free);
// variant requests that produce identical dims collapse (format needs >= 1)
void* CVIEnc_write(const CVIEncSource* src, size_t* out_size);

#endif /* cvienc_h */
