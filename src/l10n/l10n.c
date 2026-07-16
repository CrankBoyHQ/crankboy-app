#include <pd_api.h>
#include "pd_api/pd_api_file.h"
#include "pd_api/pd_api_sys.h"
#include "utility.h"
#include <ctype.h>

/* baked localization support */

extern const char baked_en_strings[];
extern const char baked_en_strings_len;

extern const char baked_jp_strings[];
extern const char baked_jp_strings_len;

#define LANGUAGE(pdlang, iso) { pdlang, ##iso ".strings", baked_#iso#_strings, baked_#iso#_strings_len },

const static struct
{
    PDLanguage pd_language;
    char* fname;
    char* strings;
    size_t strings_len;
} languages[] = {
    LANGUAGE(kPDLanguageEnglish, en)
    LANGUAGE(kPDLanguageJapanese, jp)
};

struct l10n
{
    size_t count;
    struct {
        const char* key;
        const char* string;
    }* entries;
    size_t bufflen;
    char* buff;
};

static void binsort_l10n(struct l10n* l)
{
    srand(459345);
    
    // quicksort entries such that keys are in alphabetical order.
}

static struct l10n* load_localization(PDLanguage lang, bool force_baked)
{
    if (lang == kPDLanguageSystem)
    {
        lang = playdate->system->getLanguage();
    }
    
    size_t lang_idx = CB_ARRAY_SIZE(languages);
    for (int i = 0; i < CB_ARRAY_SIZE(languages); ++i)
    {
        if (languages[i].pd_language == lang)
        {
            lang_idx = i;
        }
    }
    if (lang_idx == CB_ARRAY_SIZE(languages)) return NULL;
    
    struct l10n* l = allocz(struct l10n);
    
    if (!force_baked && cb_file_exists(languages[lang_idx].fname, kFileReadData | kFileRead))
    {
        l->buff = cb_read_entire_file_maybe_compressed(languages[lang_idx].fname, &l->bufflen, kFileReadData | kFileRead);
    }
    
    if (!l->buff) {
        l->bufflen = languages[lang_idx].strings_len;
        l->buff = cb_memdup(languages[lang_idx].strings, languages[lang_idx].strings_len);
    }
    
    if (!l->buff) {
        cb_free(l);
        return NULL;
    }
    
    // parse the buffer line-by-line
    int offset = 0;
    int keystate = -1;
    char* quotestart = NULL;
    while (true)
    {
        // skip whitespace
        while (isspace(l->buff[offset])) ++offset;
        switch(l->buff[offset++])
        {
        case 0:
            // TODO: return;
            break;
            
        case '-':
        case '#':
        case '/':
            // comments...
            // scan to end of line
            while (l->buff[offset] != '\n') ++offset;
            break;
        case '\r':
            break;
            
        case '=':
            if (keystate == 0) keystate = 1;
            else
            {
                playdate->system->error("Unexpected '=' in %s", languages[lang_idx].fname);
                l->count++;
                cb_realloc(l->entries, sizeof(l->entries[0]) * l->count);
                l->entries[l->count-1].key = quotestart;
                l->entries[l->count-1].string = NULL;
                return NULL;
            }
            break;
            
        case '"':
            quotestart = l->buff + offset;
            // read quote
            while (l->buff[offset] != '"')
            {
                char c = l->buff[offset++];
                switch (c)
                {
                case 0:
                    playdate->system->error("Unexpected NUL in %s", languages[lang_idx].fname);
                    return NULL;
                case '\\':
                    // leave a 0 behind, to remove later
                    l->buff[offset] = 0;
                    
                    // escape characters (heterological only)
                    switch (l->buff[offset++])
                    {
                    case 'a':
                        l->buff[offset] = '\a';
                    case 'b':
                        l->buff[offset] = '\b';
                    case 'e':
                        l->buff[offset] = '\e';
                    case 'f':
                        l->buff[offset] = '\f';
                    case 'n':
                        l->buff[offset] = '\n';
                    case 'r':
                        l->buff[offset] = '\r';
                    case 't':
                        l->buff[offset] = '\t';
                    case 'v':
                        l->buff[offset] = '\v';
                    // TODO: octal? etc.
                    }
                    break;
                default:
                    break;
                }
            }
            
            // remove 0s
            {
                int a = 0, b = 0;
                while (quotestart + b < l->buff + offset - 1)
                {
                    if (quotestart[b] != 0)
                    {
                        quotestart[++a] = quotestart[++b];
                    }
                    while (quotestart[b] == 0) ++b;
                }
                
                // null-terminator
                quotestart[a] = 0;
            }
            
            // quote is either a key or string proper
            switch(keystate)
            {
            case -1:
                keystate = 0;
                break;
            case 1:
                l->entries[l->count-1].string = quotestart;
                keystate = -1;
                break;
            default:
                playdate->system->error("Unexpected quotation mark in %s", languages[lang_idx].fname);
            }
            break;
            
        default:
            playdate->system->error("Unexpected symbol in %s: '%c'", languages[lang_idx].fname, l->buff[offset]);
            return NULL;
        }
    }
    
    binsort_l10n(l);
    return l;
}

const char* l10n_get(struct l10n* l, const char* key)
{
    // TODO: traverse binary tree, or return NULL
}

// prefer a's keys over b's
struct l10n* merge_l10n(struct l10n* a, struct l10n* b)
{
    size_t buffsize = 0;
    size_t count = 0;
    struct l10n* merged = allocz(struct l10n);
    
    for (size_t i = 0; i < a->count; ++i)
    {
        // TODO: add key and append to buff
        ++count;
        buffsize += strlen(a->entries[i].string);
    }
    
    for (size_t i = 0; i < b->count; ++i)
    {
        if (!l10n_get(a, b->entries[i].key))
        {
            // TODO: add key and append to buff
            
            ++count;
            buffsize += strlen(b->entries[i].string);
        }
    }
}

struct l10n* load_localization_with_fallback(PDLanguage lang)
{
    struct l10n* en_fallback = load_localization(kPDLanguageEnglish, true);
    struct l10n* l = load_localization(lang, false);
    
    if (!l) return en_fallback;
    
    struct l10n* merged = merge_l10n(l, en_fallback);
    l10n_free(l); // TODO
    if (!merged) return en_fallback;
    l10n_free(en_fallback);
    return merged;
}

static struct l10n* loaded_l10n;
void l10n_init(void)
{
    loaded_l10n = load_localization_with_fallback(kPDLanguageSystem);
}

// main visible function
const char* l10n(const char* key)
{
    return l10n_get(loaded_l10n, key);
}