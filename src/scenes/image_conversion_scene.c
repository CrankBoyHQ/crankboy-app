#include "image_conversion_scene.h"

#include "../app.h"
#include "../cvienc.h"
#include "../pdi.h"
#include "../utility.h"
#include "library_scene.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wconversion"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPG
#define STBI_ONLY_JPEG
#define STBI_NO_THREAD_LOCALS
#include "../../libs/stb_image.h"
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
static const int matrix_floyd_steinberg_x = 1;

#define MATRIX_FLOYD_STEINBERG_HEIGHT 2
#define FW_BITS 10
#define FW_ONE (1 << FW_BITS)
#define FW_HALF (FW_ONE / 2)
typedef int16_t fw_t;
typedef int32_t fw32_t;

const int GRAYDIV = WEIGHT_DIVISOR / FW_ONE;

__space static inline fw_t rgba_to_gray(unsigned char* rgba)
{
    unsigned r = rgba[0];
    unsigned g = rgba[1];
    unsigned b = rgba[2];
    unsigned gray = r * WEIGHT_R + g * WEIGHT_G + b * WEIGHT_B;
    return gray / GRAYDIV;
}

__space static void get_image_statistics(
    unsigned char* rgba, unsigned in_width, unsigned in_height, fw_t* o_darkest, fw_t* o_brightest,
    fw_t* o_avg
)
{
    uint32_t avg = 0;
    *o_darkest = FW_ONE;
    *o_brightest = 0;

    for (int i = 0; i < in_width * in_height; ++i)
    {
        fw_t gray = rgba_to_gray((uint8_t*)rgba + i * 4);
        if (gray < *o_darkest)
            *o_darkest = gray;
        if (gray > *o_brightest)
            *o_brightest = gray;
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
    const int mh = MATRIX_FLOYD_STEINBERG_HEIGHT;
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
    float dva = 1.0f / ((v - l) * (v - h));
    float va = (l * h) * dva;
    float vb = (-l - h) * dva;
    float vc = 1 * dva;

    // coefficients of a parabola that passes through
    // (l, 0), (v, 0), (h, 1)
    float dha = 1.0f / ((h - l) * (h - v));
    float ha = (l * v) * dha;
    float hb = (-l - v) * dha;
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

    fw_t* error = cb_malloc(mh * out_width * sizeof(fw_t));
    if (!error)
        return false;
    memset(error, 0, mh * out_width * sizeof(fw_t));

    for (unsigned y = 0; y < out_height; ++y)
    {
        int err_row_idx[MATRIX_FLOYD_STEINBERG_HEIGHT];
        for (int i = 0; i < mh; ++i)
            err_row_idx[i] = (error_row + i) % mh;
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
                g = fwa + (fwb * g) / FW_ONE + (c * g / FW_ONE * g / FW_ONE);
#else
                float fg = g / (float)FW_ONE;
                g = FW_ONE * (a + fg * b + fg * c * fg);
#endif
            }

            if (g < 0)
                g = 0;
            if (g > FW_ONE)
                g = FW_ONE;

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
                        int c = matrix[i * mw + j];
                        error[err_row_idx[i] * out_width + j + x - mx] += c * ediff;
                    }
                }
            }
        }

        // reset this error row (for next row's use)
        memset(&error[err_row_idx[0] * out_width], 0, err_stride);
    }

    cb_free(error);
    return true;
}

unsigned char* cb_decode_png(
    const char* context_filename, const void* png_data, int png_size, int* out_width,
    int* out_height
)
{
    int channels;
    unsigned char* img_data = stbi_load_from_memory(
        (const stbi_uc*)png_data, png_size, out_width, out_height, &channels, 4
    );

    if (!img_data)
    {
        spoolError(
            "Failed to decode %s -- is this file corrupt or encoded strangely, perhaps?",
            context_filename
        );
        return NULL;
    }

    return img_data;
}

void cb_free_decoded_image(unsigned char* img_data)
{
    if (img_data)
        stbi_image_free(img_data);
}

void* rgba_to_pdi(
    const char* context_filename, unsigned char* img_data, int width, int height, size_t* out_size,
    int max_width, int max_height
)
{
    float wscale = 1.0f, hscale = 1.0f;
    if (max_width >= 0 && max_width < width)
    {
        wscale = (float)width / (float)max_width;
    }
    else
    {
        max_width = width;
    }
    if (max_height >= 0 && max_height < height)
    {
        hscale = (float)height / (float)max_height;
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
        spoolError("Failed to decode %s -- requested dimension 0", context_filename);
        return NULL;
    }

    size_t stride = ((max_width + 31) / 32) * 4;

    struct PDIHeader header;
    memcpy(header.magic, PDI_MAGIC, sizeof(header.magic));
    header.flags = 0;

    struct PDICell cell;
    cell.clip_width = max_width;
    cell.clip_height = max_height;
    cell.stride = stride;
    cell.clip_left = 0;
    cell.clip_right = 0;
    cell.clip_top = 0;
    cell.clip_bottom = 0;
    cell.flags = 0;

    size_t white_size = stride * max_height;
    size_t total_size = sizeof(header) + sizeof(cell) + white_size;

    void* pdi_data = cb_malloc(total_size);
    if (!pdi_data)
    {
        spoolError("Failed to allocate memory for PDI for %s", context_filename);
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

    errdiff_dither(img_data, width, height, ptr, max_width, max_height, stride, scale, 0.95f);

    if (out_size)
    {
        *out_size = total_size;
    }
    return pdi_data;
}

void* png_to_cvi(const char* context_filename, const void* png_data, int png_size, size_t* out_size)
{
    int width, height;
    unsigned char* img_data = cb_decode_png(context_filename, png_data, png_size, &width, &height);
    if (!img_data)
        return NULL;

    // same variant set the downloaded repo covers carry: a 240-fit box
    // for the library detail panel + a height-exact 200 for coverflow
    // and the pause menu
    static const CVIEncRequest reqs[2] = {
        {240, CVI_ENC_FIT_BOX},
        {200, CVI_ENC_HEIGHT_EXACT},
    };
    CVIEncSource src = {
        .rgba = img_data, .width = width, .height = height, .requests = reqs, .num_requests = 2
    };
    void* cvi_data = CVIEnc_write(&src, out_size);

    cb_free_decoded_image(img_data);
    return cvi_data;
}

// returns 1 on success
// returns 0 on failure
// returns -1 if file not found (i.e. not in PDX directory)
static int process_png(const char* fname)
{
    size_t len;
    void* data = cb_read_entire_file(fname, &len, kFileReadData);
    if (!data)
        return -1;

    bool success = false;

    size_t cvi_len = 0;
    void* cvi = png_to_cvi(fname, data, len, &cvi_len);
    cb_free(data);

    if (cvi && cvi_len > 0)
    {
        char* basename = cb_basename(fname, true);
        char* cvi_name = aprintf("%s/%s.cvi", cb_gb_directory_path(CB_coversPath), basename);
        if (basename)
            cb_free(basename);

        if (cvi_name)
        {
            if (cb_write_entire_file(cvi_name, cvi, cvi_len))
            {
                success = true;
            }
            cb_free(cvi_name);
        }
    }

    if (cvi)
    {
        cb_free(cvi);
    }

    if (success)
    {
        char* new_fname = aprintf("%s.conv", fname);
        if (new_fname)
        {
            playdate->file->rename(fname, new_fname);
            cb_free(new_fname);
        }
    }
    else
    {
        char* new_fname = aprintf("%s.fail", fname);
        if (new_fname)
        {
            playdate->file->rename(fname, new_fname);
            cb_free(new_fname);
        }
    }

    return success;
}

void CB_ImageConversionScene_update(void* object, uint32_t u32enc_dt)
{
    if (CB_App->pendingScene)
    {
        return;
    }

    CB_ImageConversionScene* convScene = object;
    float dt = UINT32_AS_FLOAT(u32enc_dt);

    switch (convScene->state)
    {
    case kStateListingFiles:
    {
        cb_draw_logo_screen_to_buffer(CB_App->subheadFont, T(status_scanning_images));

        playdate->file->listfiles(
            cb_gb_directory_path(CB_coversPath), on_list_file, convScene, false
        );

        convScene->state = kStateDone;

        // check if any files exist at their listed paths
        for (int i = 0; i < convScene->files_count; ++i)
        {
            const char* fpath = convScene->files[i];
            if (cb_file_exists(fpath, kFileReadData))
            {
                convScene->progress_max_width = cb_calculate_progress_max_width(
                    CB_App->subheadFont, PROGRESS_STYLE_FRACTION, convScene->files_count
                );

                convScene->state = kStateConverting;
                break;
            }
        }
        break;
    }

    case kStateConverting:
    {
        if (convScene->idx < convScene->files_count)
        {
            int current_display_index = convScene->idx + 1;

            char progress_details[50];
            snprintf(
                progress_details, sizeof(progress_details), "%d/%zu", current_display_index,
                convScene->files_count
            );

            cb_draw_logo_screen_centered_split(
                CB_App->subheadFont, T(status_converting_images), progress_details,
                convScene->progress_max_width
            );

            char* fname = convScene->files[convScene->idx];

            size_t len = strlen(fname);
            if (len > 0 && (fname[len - 1] == '\n' || fname[len - 1] == '\r'))
            {
                fname[len - 1] = '\0';
            }

            int result = process_png(fname);

            if (result >= 0)
            {
                playdate->system->logToConsole("  result: %d\n", (int)result);
            }

            convScene->idx++;
        }
        else
        {
            convScene->state = kStateDone;
        }
        break;
    }

    case kStateDone:
    {
        CB_LibraryScene* libraryScene = CB_LibraryScene_new();
        CB_present(libraryScene->scene);
        break;
    }
    }
}

void CB_ImageConversionScene_free(void* object)
{
    CB_ImageConversionScene* convScene = object;
    for (int i = 0; i < convScene->files_count; ++i)
    {
        cb_free(convScene->files[i]);
    }
    cb_free(convScene->files);
    CB_Scene_free(convScene->scene);
    cb_free(convScene);
}

bool filename_has_stbi_extension(const char* fname)
{
    return (
        cb_file_has_extension(fname, ".png") || cb_file_has_extension(fname, ".jpg") ||
        cb_file_has_extension(fname, ".jpeg") || cb_file_has_extension(fname, ".bmp")
    );
}

static void on_list_file(const char* fname, void* ud)
{
    CB_ImageConversionScene* convScene = ud;

    if (filename_has_stbi_extension(fname))
    {
        char* full_path = aprintf("%s/%s", cb_gb_directory_path(CB_coversPath), fname);
        char** new_files =
            cb_realloc(convScene->files, sizeof(char*) * (convScene->files_count + 1));

        convScene->files = new_files;
        convScene->files[convScene->files_count] = full_path;

        if (convScene->files[convScene->files_count] == NULL)
        {
            playdate->system->error("Out of memory copying filename!");
            return;
        }

        convScene->files_count++;
    }
}

CB_ImageConversionScene* CB_ImageConversionScene_new(void)
{
    CB_Scene* scene = CB_Scene_new();
    CB_ImageConversionScene* convScene = cb_malloc(sizeof(CB_ImageConversionScene));
    scene->id = "image-conversion";
    convScene->scene = scene;

    scene->managedObject = convScene;
    scene->update = CB_ImageConversionScene_update;
    scene->free = CB_ImageConversionScene_free;
    scene->use_user_stack = false;

    convScene->idx = 0;
    convScene->files = NULL;
    convScene->files_count = 0;

    convScene->state = kStateListingFiles;

    return convScene;
}
