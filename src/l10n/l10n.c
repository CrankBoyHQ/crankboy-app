#include "pd_api.h"
#include "pd_api/pd_api_file.h"
#include "pd_api/pd_api_sys.h"
#include "utility.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* baked localization support */

extern const unsigned char baked_en_strings[];
extern const unsigned int baked_en_strings_len;

extern const unsigned char baked_ja_strings[];
extern const unsigned int baked_ja_strings_len;

#define LANGUAGE(pdlang, iso) \
    {pdlang, #iso ".strings", baked_##iso##_strings, &baked_##iso##_strings_len},

static const struct
{
    PDLanguage pd_language;
    const char* fname;
    const unsigned char* strings;
    const unsigned int* strings_len;
} languages[] = {LANGUAGE(kPDLanguageEnglish, en) LANGUAGE(kPDLanguageJapanese, ja)};

struct l10n
{
    size_t count;
    struct l10n_entry
    {
        const char* key;
        const char* string;
    }* entries;
    size_t bufflen;
    char* buff;
};

static void l10n_free(struct l10n* l)
{
    if (!l)
        return;
    cb_free(l->entries);
    cb_free(l->buff);
    cb_free(l);
}

static int compare_l10n_entries(const void* a, const void* b)
{
    return strcmp(((const struct l10n_entry*)a)->key, ((const struct l10n_entry*)b)->key);
}

static void binsort_l10n(struct l10n* l)
{
    if (l->count)
        qsort(l->entries, l->count, sizeof(l->entries[0]), compare_l10n_entries);
}

// unquotes and unescapes in place, starting just past an opening '"'.
// returns offset just past the closing '"', or -1 on error.
static int parse_quoted(char* buff, int offset)
{
    char* write = buff + offset;
    while (buff[offset] != '"')
    {
        char c = buff[offset++];
        if (c == 0)
            return -1;
        if (c == '\\')
        {
            char e = buff[offset++];
            switch (e)
            {
            case 0:
                return -1;
            case 'a':
                c = '\a';
                break;
            case 'b':
                c = '\b';
                break;
            case 'e':
                c = '\e';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'v':
                c = '\v';
                break;
            default:
                c = e;
                break;
            }
        }
        *write++ = c;
    }
    *write = 0;
    return offset + 1;
}

static struct l10n* load_localization(PDLanguage lang, bool force_baked)
{
    if (lang == kPDLanguageSystem)
    {
        lang = playdate->system->getLanguage();
    }

    size_t lang_idx = CB_ARRAY_SIZE(languages);
    for (size_t i = 0; i < CB_ARRAY_SIZE(languages); ++i)
    {
        if (languages[i].pd_language == lang)
        {
            lang_idx = i;
        }
    }
    if (lang_idx == CB_ARRAY_SIZE(languages))
        return NULL;

    struct l10n* l = allocz(struct l10n);

    if (!force_baked && cb_file_exists(languages[lang_idx].fname, kFileReadData | kFileRead))
    {
        l->buff = cb_read_entire_file_maybe_compressed(
            languages[lang_idx].fname, &l->bufflen, kFileReadData | kFileRead
        );
    }

    if (!l->buff)
    {
        l->bufflen = *languages[lang_idx].strings_len;
        l->buff = cb_malloc(l->bufflen + 1);
        if (l->buff)
        {
            memcpy(l->buff, languages[lang_idx].strings, l->bufflen);
            l->buff[l->bufflen] = 0;
        }
    }

    if (!l->buff)
    {
        cb_free(l);
        return NULL;
    }

    // parse the buffer: "key" = "string", with comment lines
    int offset = 0;
    int keystate = -1;
    while (true)
    {
        while (isspace((unsigned char)l->buff[offset]))
            ++offset;
        switch (l->buff[offset++])
        {
        case 0:
            if (keystate != -1)
            {
                playdate->system->error("Unexpected end of %s", languages[lang_idx].fname);
                goto fail;
            }
            binsort_l10n(l);
            return l;

        case '-':
        case '#':
        case '/':
            while (l->buff[offset] != '\n' && l->buff[offset] != 0)
                ++offset;
            break;

        case '=':
            if (keystate != 0)
            {
                playdate->system->error("Unexpected '=' in %s", languages[lang_idx].fname);
                goto fail;
            }
            keystate = 1;
            break;

        case ';':
            if (keystate != -1)
            {
                playdate->system->error("Unexpected ';' in %s", languages[lang_idx].fname);
                goto fail;
            }
            break;

        case '"':
        {
            char* quotestart = l->buff + offset;
            offset = parse_quoted(l->buff, offset);
            if (offset < 0)
            {
                playdate->system->error("Unterminated string in %s", languages[lang_idx].fname);
                goto fail;
            }

            switch (keystate)
            {
            case -1:
                l->count++;
                l->entries = cb_realloc(l->entries, sizeof(l->entries[0]) * l->count);
                l->entries[l->count - 1].key = quotestart;
                l->entries[l->count - 1].string = NULL;
                keystate = 0;
                break;
            case 1:
                l->entries[l->count - 1].string = quotestart;
                keystate = -1;
                break;
            default:
                playdate->system->error(
                    "Unexpected quotation mark in %s", languages[lang_idx].fname
                );
                goto fail;
            }
            break;
        }

        default:
            playdate->system->error(
                "Unexpected symbol in %s: '%c'", languages[lang_idx].fname, l->buff[offset - 1]
            );
            goto fail;
        }
    }

fail:
    l10n_free(l);
    return NULL;
}

static const char* l10n_get(struct l10n* l, const char* key)
{
    if (!l)
        return NULL;

    size_t lo = 0, hi = l->count;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(key, l->entries[mid].key);
        if (c == 0)
            return l->entries[mid].string;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

static void append_l10n_entry(struct l10n* m, const char* key, const char* string)
{
    m->entries[m->count].key = m->buff + m->bufflen;
    strcpy(m->buff + m->bufflen, key);
    m->bufflen += strlen(key) + 1;

    m->entries[m->count].string = m->buff + m->bufflen;
    strcpy(m->buff + m->bufflen, string);
    m->bufflen += strlen(string) + 1;

    m->count++;
}

// prefer a's keys over b's
static struct l10n* merge_l10n(struct l10n* a, struct l10n* b)
{
    size_t buffsize = 0;
    size_t count = 0;

    for (size_t i = 0; i < a->count; ++i)
    {
        if (a->entries[i].string[0] == 0)
            continue;
        ++count;
        buffsize += strlen(a->entries[i].key) + strlen(a->entries[i].string) + 2;
    }

    for (size_t i = 0; i < b->count; ++i)
    {
        if (!l10n_get(a, b->entries[i].key))
        {
            ++count;
            buffsize += strlen(b->entries[i].key) + strlen(b->entries[i].string) + 2;
        }
    }

    struct l10n* merged = allocz(struct l10n);
    merged->buff = cb_malloc(buffsize);
    merged->entries = cb_malloc(sizeof(merged->entries[0]) * count);
    if ((buffsize && !merged->buff) || (count && !merged->entries))
    {
        l10n_free(merged);
        return NULL;
    }

    for (size_t i = 0; i < a->count; ++i)
    {
        if (a->entries[i].string[0] == 0)
            continue;
        append_l10n_entry(merged, a->entries[i].key, a->entries[i].string);
    }

    for (size_t i = 0; i < b->count; ++i)
    {
        if (!l10n_get(a, b->entries[i].key))
        {
            append_l10n_entry(merged, b->entries[i].key, b->entries[i].string);
        }
    }

    binsort_l10n(merged);
    return merged;
}

static struct l10n* load_localization_with_fallback(PDLanguage lang)
{
    struct l10n* en_fallback = load_localization(kPDLanguageEnglish, true);
    struct l10n* l = load_localization(lang, false);

    if (!l)
        return en_fallback;
    if (!en_fallback)
        return l;

    struct l10n* merged = merge_l10n(l, en_fallback);
    l10n_free(l);
    if (!merged)
        return en_fallback;
    l10n_free(en_fallback);
    return merged;
}

static struct l10n* loaded_l10n;
void CB_load_strings(PDLanguage lang)
{
    l10n_free(loaded_l10n);
    loaded_l10n = load_localization_with_fallback(lang);
}

// main visible function
const char* l10n(const char* key)
{
    const char* s = l10n_get(loaded_l10n, key);
    return s ? s : key;
}
