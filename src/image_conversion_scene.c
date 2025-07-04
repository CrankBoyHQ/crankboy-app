#include "image_conversion_scene.h"

#include "app.h"
#include "library_scene.h"
#include "pdi.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wconversion"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPG
#define STBI_ONLY_JPEG
#define STBI_NO_THREAD_LOCALS
#include "../stb_image.h"
#pragma GCC diagnostic pop

// apparently these are standard weights.
#define WEIGHT_R 312
#define WEIGHT_G 591
#define WEIGHT_B 126
#define WEIGHT_DIVISOR (256 * 1024)

// clang-format off
static const int matrix_floyd_steinberg[] = {
    0, 0, 7,
    3, 5, 1
};
// clang-format on

static void on_list_file(const char* fname, void* ud);

static const int matrix_floyd_steinberg_divisor = 16;
static const int matrix_floyd_steinberg_width = 3;
static const int matrix_floyd_steinberg_height = 2;
static const int matrix_floyd_steinberg_x = 1;

#define FW_BITS 10
#define FW_ONE (1 << FW_BITS)
#define FW_HALF (FW_ONE / 2)
typedef int16_t fw_t;
typedef int32_t fw32_t;

const int GRAYDIV = WEIGHT_DIVISOR / FW_ONE;
    
__space static inline
fw_t rgba_to_gray(unsigned char* rgba)
{
    unsigned r = rgba[0];
    unsigned g = rgba[1];
    unsigned b = rgba[2];
    unsigned gray = r * WEIGHT_R + g * WEIGHT_G + b * WEIGHT_B;
    return gray / GRAYDIV;
}
    
__space static void get_image_statistics(
    unsigned char* rgba, unsigned in_width, unsigned in_height,
    fw_t* o_darkest, fw_t* o_brightest, fw_t* o_avg
) {
    uint32_t avg = 0;
    *o_darkest = FW_ONE;
    *o_brightest = 0;
    
    for (int i = 0; i < in_width * in_height; ++i)
    {
        fw_t gray = rgba_to_gray((uint8_t*)rgba + i*4);
        if (gray < *o_darkest) *o_darkest = gray;
        if (gray > *o_brightest) *o_brightest = gray;
        avg += gray;
    }
    
    *o_avg = avg / (in_width * in_height);
}

// returns false on error
__space bool errdiff_dither(
    unsigned char* rgba, unsigned in_width, unsigned in_height,
    uint8_t* out,  // 32-bit packed 1-bit
    unsigned out_width, unsigned out_height, size_t out_stride, float scale,
    float brightness_compensatation
)
// fixed-width precision
{
    const int* const matrix = matrix_floyd_steinberg;
    const int mdiv = matrix_floyd_steinberg_divisor;
    const int mw = matrix_floyd_steinberg_width;
    const int mh = matrix_floyd_steinberg_height;
    const int mx = matrix_floyd_steinberg_x;
    
    fw_t lo, hi, avg;
    get_image_statistics(rgba, in_width, in_height, &lo, &hi, &avg);
    
    // lo/hi are at most/min 5%/95%
    lo = MIN(lo, FW_ONE * 0.05f);
    hi = MAX(lo, FW_ONE * 0.95f);
    
    // avg is at most/min 20%/80%
    avg = MAX(avg, FW_ONE * 0.2f);
    avg = MIN(avg, FW_ONE * 0.8f);
        
    lo = brightness_compensatation * lo + (1 - brightness_compensatation) * 0;
    hi = brightness_compensatation * hi + (1 - brightness_compensatation) * FW_ONE;
    avg = brightness_compensatation * avg + (1 - brightness_compensatation) * FW_ONE / 2;
    
    float l = lo / (float)FW_ONE;
    float h = hi / (float)FW_ONE;
    float v = avg / (float)FW_ONE;
    
    // coefficients of a parabola that passes through
    // (l, 0), (v, 1), (h, 0)
    float dva = 1.0f / ((v-l)*(v-h));
    float va = (l*h) * dva;
    float vb = (-l-h) * dva;
    float vc = 1 * dva;
    
    // coefficients of a parabola that passes through
    // (l, 0), (v, 0), (h, 1)
    float dha = 1.0f / ((h-l)*(h-v));
    float ha = (l*v) * dha;
    float hb = (-l-v) * dha;
    float hc = 1 * dha;
    
    // coefficients of a parabola that passes through
    // (l, 0), (v, 0.5), (h, 1)
    float a = va * 0.5f + ha;
    float b = vb * 0.5f + hb;
    float c = vc * 0.5f + hc;
    
#if 0
    printf("l=%f, v=%f, h=%f\n", l, v, h);
    printf("[v] l->%f, v->%f, h->%f\n", va + vb*l + vc*l*l, va + vb*v + vc*v*v, va + vb*h + vc*h*h);
    printf("[h] l->%f, v->%f, h->%f\n", ha + hb*l + hc*l*l, ha + hb*v + hc*v*v, ha + hb*h + hc*h*h);
    printf("l->%f, v->%f, h->%f\n", a + b*l + c*l*l, a + b*v + c*v*v, a + b*h + c*h*h);
#endif
    
    fw32_t fwa = a * FW_ONE;
    fw32_t fwb = b * FW_ONE;
    fw32_t fwc = c * FW_ONE;
    

    assert(WEIGHT_DIVISOR >= FW_ONE);

    const unsigned err_stride = sizeof(fw_t) * out_width;

    int error_row = 0;

    fw_t* error = malloc(sizeof(fw_t) * err_stride);
    if (!error)
        return false;
    memset(error, 0, sizeof(fw_t) * err_stride);

    for (unsigned y = 0; y < out_height; ++y)
    {
        int err_row_idx[mh];
        for (int i = 0; i < mh; ++i)
            err_row_idx[i] = (error_row = i) % mh;
        error_row = (error_row + 1) % mh;

        for (unsigned x = 0; x < out_width; ++x)
        {
            unsigned xb = x / 8;
            unsigned x8 = x % 8;

            // get graytone value for pixel
            int ix = MIN(x * scale, in_width - 1);
            int iy = MIN(y * scale, in_height - 1);
            int src_idx = (iy * in_width + ix) * 4;

            fw_t g = rgba_to_gray((uint8_t*)rgba + src_idx);
            
            // apply brightness-curve transformation
            {
                #ifdef USE_FW_BRIGHTNESS_CURVE
                g = fwa + (fwb * g) / FW_ONE + (c*g/FW_ONE*g/FW_ONE);
                #else
                float fg = g / (float)FW_ONE;
                g = FW_ONE * (a + fg*b + fg*c*fg);
                #endif
            }
            
            if (g < 0) g = 0;
            if (g > FW_ONE) g = FW_ONE;
            
            fw_t e = error[err_row_idx[0] * out_width + x] / mdiv;
            fw_t ediff;
            if (g + e > FW_HALF)
            {
                ediff = (g + e) - FW_ONE;

                out[out_stride * y + xb] |= (1 << (7 - x8));
            }
            else
            {
                ediff = (g + e);
            }

            // diffuse error
            for (int i = 0; i < mh; ++i)
            {
                for (int j = 0; j < mw; ++j)
                {
                    if (j + x - mx >= 0 && j + x - mx < out_width)
                    {
                        int c = matrix[i * mh + j];
                        error[err_row_idx[i] * out_width + j + x - mx] += c * ediff;
                    }
                }
            }
        }

        // reset this error row (for next row's use)
        memset(&error[err_row_idx[0] * out_width], 0, err_stride);
    }

    free(error);
    return true;
}

void* png_to_pdi(
    const void* png_data, int png_size, size_t* out_size, int max_width, int max_height
)
{
    int width, height, channels;
    unsigned char* img_data =
        stbi_load_from_memory((const stbi_uc*)png_data, png_size, &width, &height, &channels, 4);

    if (!img_data)
    {
        return NULL;
    }

    float wscale = 1.0f, hscale = 1.0f;
    if (max_width >= 0 && max_width < width)
    {
        wscale = (float)width / (float)max_width;
        printf("image width: %d; desired: %d\n", width, max_width);
    }
    else
    {
        max_width = width;
    }
    if (max_height >= 0 && max_height < height)
    {
        hscale = (float)height / (float)max_height;
        printf("image height: %d; desired: %d\n", height, max_height);
    }
    else
    {
        max_height = height;
    }

    float scale = MAX(wscale, hscale);

    // crop image horizontally
    if (width / scale + 0.75f < max_width - 1)
    {
        max_width = width / scale + 0.75f;
    }

    // crop image vertically
    else if (height / scale + 0.75f < max_height - 1)
    {
        max_height = height / scale + 0.75f;
    }

    if (max_width == 0 || max_height == 0)
    {
        stbi_image_free(img_data);
        return NULL;
    }

    // Determine if we have transparency
    int has_transparency = false;
#if 0
    if (channels == 4) {
        // Check if any pixel has alpha < 255
        for (int i = 0; i < width * height * 4; i += 4) {
            if (img_data[i + 3] < 255) {
                has_transparency = 1;
                break;
            }
        }
    }
#endif

    size_t stride = ((max_width + 31) / 32) * 4;

    printf("stride: %x\n", (int)stride);

    struct PDIHeader header;
    memcpy(header.magic, PDI_MAGIC, sizeof(header.magic));
    header.flags = 0;  // TODO: compression

    struct PDICell cell;
    // TODO: clip rect
    cell.clip_width = max_width;
    cell.clip_height = max_height;
    cell.stride = stride;
    cell.clip_left = 0;
    cell.clip_right = 0;
    cell.clip_top = 0;
    cell.clip_bottom = 0;
    cell.flags = has_transparency ? PDI_CELL_FLAG_TRANSPARENCY : 0;

    size_t white_size = stride * max_height;
    size_t opaque_size = has_transparency ? stride * max_height : 0;
    size_t total_size = sizeof(header) + sizeof(cell) + white_size + opaque_size;

    void* pdi_data = malloc(total_size);
    if (!pdi_data)
    {
        stbi_image_free(img_data);
        return NULL;
    }

    memset(pdi_data, 0, total_size);

    uint8_t* ptr = (uint8_t*)pdi_data;

    // Write header
    memcpy(ptr, &header, sizeof(header));
    ptr += sizeof(header);

    // Write cell
    memcpy(ptr, &cell, sizeof(cell));
    ptr += sizeof(cell);

    uint32_t lfsr = 0x8389E83A;

    // Write white data (convert RGBA to grayscale)
#ifdef LFSR_DITHER
    for (int y = 0; y < max_height; y++)
    {
        for (int x = 0; x < max_width; x++)
        {
            lfsr <<= 1;
            lfsr |= 1 & ((lfsr >> 1) ^ (lfsr >> 5) ^ (lfsr >> 8) ^ (lfsr >> 21) ^ (lfsr >> 31) ^ 1);

            unsigned xb = x / 8;
            unsigned x8 = x % 8;

            int ix = MIN(x * scale, width - 1);
            int iy = MIN(y * scale, height - 1);

            int src_idx = (iy * width + ix) * 4;
            unsigned r = img_data[src_idx];
            unsigned g = img_data[src_idx + 1];
            unsigned b = img_data[src_idx + 2];

            // apparently these are standard weights.
            // range is (0, 256*1024 + margin]
            unsigned gray = r * WEIGHT_R + g * WEIGHT_G + b * WEIGHT_B;

            // TODO: use bayer filtering or something
            unsigned white = gray > lfsr % (WEIGHT_DIVISOR);

            if (white)
            {
                ptr[y * stride + xb] |= (1 << (7 - x8));
            }
        }
    }
#else
    errdiff_dither(img_data, width, height, ptr, max_width, max_height, stride, scale, 0.95f);
#endif
    ptr += white_size;

    if (has_transparency)
    {
        for (int y = 0; y < max_height; y++)
        {
            for (int x = 0; x < max_width; x++)
            {
                unsigned xb = x / 8;
                unsigned x8 = x % 8;

                int ix = MIN(x * scale, width - 1);
                int iy = MIN(y * scale, height - 1);

                int src_idx = (iy * width + ix) * 4;

                int a = img_data[src_idx + 3];

                if (a > 32)
                {
                    ptr[y * stride + xb] |= (1 << (7 - x8));  // Alpha channel
                }
            }
        }
    }

    // Clean up
    stbi_image_free(img_data);

    if (out_size)
    {
        *out_size = total_size;
    }
    return pdi_data;
}

// returns true on success
static bool process_png(const char* fname)
{
    size_t len;
    void* data = pgb_read_entire_file(fname, &len, kFileReadData);
    bool success = false;

    if (data)
    {
        size_t pdi_len = 0;
        void* pdi = png_to_pdi(data, len, &pdi_len, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
        free(data);

        if (pdi && pdi_len > 0)
        {
            char* basename = pgb_basename(fname, true);
            char* pdi_name = aprintf("%s/%s.pdi", PGB_coversPath, basename);
            free(basename);

            if (pgb_write_entire_file(pdi_name, pdi, pdi_len))
            {
                playdate->file->unlink(fname, false);
                success = true;
            }

            free(pdi_name);
        }

        if (pdi)
        {
            free(pdi);
        }
    }

    return success;
}

void PGB_ImageConversionScene_update(void* object, uint32_t u32enc_dt)
{
    PGB_ImageConversionScene* convScene = object;
    float dt = UINT32_AS_FLOAT(u32enc_dt);

    switch (convScene->state)
    {
    case kStateListingFiles:
    {
        pgb_draw_logo_with_message("Scanning for new images…");

        pgb_listfiles(PGB_coversPath, on_list_file, convScene, true, kFileReadData);

        if (convScene->files_count == 0)
        {
            convScene->state = kStateDone;
        }
        else
        {
            convScene->state = kStateConverting;
        }
        break;
    }

    case kStateConverting:
    {
        if (convScene->idx < convScene->files_count)
        {
            char* fname = convScene->files[convScene->idx++];

            size_t len = strlen(fname);
            if (len > 0 && (fname[len - 1] == '\n' || fname[len - 1] == '\r'))
            {
                fname[len - 1] = '\0';
            }

            char* full_fname = aprintf("%s/%s", PGB_coversPath, fname);

            char* progress_msg = aprintf(
                "Converting image (%d of %d) to .pdi…", (int)convScene->idx,
                (int)convScene->files_count
            );

            if (progress_msg)
            {
                pgb_draw_logo_with_message(progress_msg);
                free(progress_msg);
            }

            bool result = process_png(full_fname);
            free(full_fname);

            printf("  result: %d\n", (int)result);
        }
        else
        {
            convScene->state = kStateDone;
        }
        break;
    }

    case kStateDone:
    {
        pgb_draw_logo_with_message("Loading Library…");

        PGB_LibraryScene* libraryScene = PGB_LibraryScene_new();
        PGB_present(libraryScene->scene);
        break;
    }
    }
}

void PGB_ImageConversionScene_free(void* object)
{
    PGB_ImageConversionScene* convScene = object;
    for (int i = 0; i < convScene->files_count; ++i)
    {
        free(convScene->files[i]);
    }
    free(convScene->files);
    free(convScene);
}

bool filename_has_stbi_extension(const char* fname)
{
    return (
        endswithi(fname, ".png") || endswithi(fname, ".jpg") || endswithi(fname, ".jpeg") ||
        endswithi(fname, ".bmp")
    );
}

static void on_list_file(const char* fname, void* ud)
{
    PGB_ImageConversionScene* convScene = ud;

    if (fname[0] == '.' || strcasecmp(fname, "Thumbs.db") == 0)
    {
        return;
    }

    if (filename_has_stbi_extension(fname))
    {
        char** new_files = realloc(convScene->files, sizeof(char*) * (convScene->files_count + 1));

        if (new_files == NULL)
        {
            playdate->system->error("Out of memory listing files!");
            return;
        }

        convScene->files = new_files;
        convScene->files[convScene->files_count] = strdup(fname);

        if (convScene->files[convScene->files_count] == NULL)
        {
            playdate->system->error("Out of memory copying filename!");
            return;
        }

        convScene->files_count++;
    }
}

PGB_ImageConversionScene* PGB_ImageConversionScene_new(void)
{
    PGB_Scene* scene = PGB_Scene_new();
    PGB_ImageConversionScene* convScene = pgb_malloc(sizeof(PGB_ImageConversionScene));
    convScene->scene = scene;

    scene->managedObject = convScene;
    scene->update = PGB_ImageConversionScene_update;
    scene->free = PGB_ImageConversionScene_free;
    scene->use_user_stack = false;

    convScene->idx = 0;
    convScene->files = NULL;
    convScene->files_count = 0;

    convScene->state = kStateListingFiles;

    return convScene;
}
