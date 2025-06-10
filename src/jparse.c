#include "jparse.h"

#include "pd_api.h"
#include "utility.h"

__section__(".rare") void SI_willDecodeSublist(json_decoder *decoder,
                                               const char *name,
                                               json_value_type type)
{
    if (type == kJSONArray)
    {
        decoder->userdata = playdate->system->realloc(NULL, sizeof(JsonArray));
        memset(decoder->userdata, 0, sizeof(JsonArray));
    }
    else
    {
        decoder->userdata = playdate->system->realloc(NULL, sizeof(JsonObject));
        memset(decoder->userdata, 0, sizeof(JsonObject));
    }
}

__section__(".rare") void SI_didDecodeArrayValue(json_decoder *decoder, int pos,
                                                 json_value value)
{
    --pos;  // one-indexed (!!)
    JsonArray *array = decoder->userdata;
    int n = array ? array->n : 0;
    if (pos >= n)
        n = pos + 1;
    size_t p2n = next_pow2(n);

    array = playdate->system->realloc(
        array, sizeof(JsonArray) + p2n * sizeof(json_value));

    if (value.type == kJSONString)
    {
        // we need to own the string
        value.data.stringval = strdup(value.data.stringval);
    }

    array->data[pos] = value;
    array->n = n;
    decoder->userdata = array;
    return;
}

__section__(".rare") void SI_didDecodeTableValue(json_decoder *decoder,
                                                 const char *key,
                                                 json_value value)
{
    JsonObject *obj = decoder->userdata;

    int n = 1 + (obj ? obj->n : 0);

    size_t p2n = next_pow2(n);

    obj = playdate->system->realloc(
        obj, sizeof(JsonObject) + p2n * sizeof(TableKeyPair));

    if (value.type == kJSONString)
    {
        // we need to own the string
        value.data.stringval = strdup(value.data.stringval);
    }

    obj->data[n - 1].key = strdup(key);
    obj->data[n - 1].value = value;
    obj->n = n;
    decoder->userdata = obj;
    return;
}

__section__(".rare") void *SI_didDecodeSublist(json_decoder *decoder,
                                               const char *name,
                                               json_value_type type)
{
    return decoder->userdata;
}

__section__(".rare") void free_json_data(json_value v)
{
    if (v.type == kJSONArray)
    {
        JsonArray *array = (JsonArray *)v.data.arrayval;
        for (size_t i = 0; i < array->n; i++)
        {
            free_json_data(array->data[i]);
        }
        free(array);
    }
    else if (v.type == kJSONTable)
    {
        JsonObject *obj = (JsonObject *)v.data.tableval;
        for (size_t i = 0; i < obj->n; i++)
        {
            free(obj->data[i].key);
            free_json_data(obj->data[i].value);
        }
        free(obj);
    }
    else if (v.type == kJSONString)
    {
        free(v.data.stringval);
    }
}

__section__(".rare") static void decodeError(struct json_decoder *decoder,
                                             const char *error, int linenum)
{
    playdate->system->logToConsole("Error decoding json: %s", error);
}

__section__(".rare") int parse_json(const char *path, json_value *out)
{
    if (!out)
        return 0;
    out->type = kJSONNull;

    SDFile *file = playdate->file->open(path, kFileRead);
    if (!file)
    {
        return 0;
    };

    struct json_decoder decoder = {
        .decodeError = decodeError,
        .willDecodeSublist = SI_willDecodeSublist,
        .shouldDecodeTableValueForKey = NULL,
        .didDecodeTableValue = SI_didDecodeTableValue,
        .shouldDecodeArrayValueAtIndex = NULL,
        .didDecodeArrayValue = SI_didDecodeArrayValue,
        .didDecodeSublist = SI_didDecodeSublist,
        .userdata = NULL,
        .returnString = 0,
        .path = NULL};

    // (gets binary data for json file)
    json_reader reader = {
        .read = (int (*)(void *, uint8_t *, int))playdate->file->read,
        .userdata = file};

    int ok = playdate->json->decode(&decoder, reader, out);
    playdate->file->close(file);

    if (!ok)
    {
        free_json_data(*out);
        out->type = kJSONNull;
        return 0;
    }
    return 1;
}

int read_string(const char** text, uint8_t* out, int bufsize)
{
    int maxlen = strnlen(*text, bufsize);
    if (maxlen == 0) return 0;
    memcpy(out, *text, maxlen);
    *text += maxlen;
    return maxlen;
}

__section__(".rare") int parse_json_string(const char *text, json_value *out)
{
    if (!out)
        return 0;
    out->type = kJSONNull;

    struct json_decoder decoder = {
        .decodeError = decodeError,
        .willDecodeSublist = SI_willDecodeSublist,
        .shouldDecodeTableValueForKey = NULL,
        .didDecodeTableValue = SI_didDecodeTableValue,
        .shouldDecodeArrayValueAtIndex = NULL,
        .didDecodeArrayValue = SI_didDecodeArrayValue,
        .didDecodeSublist = SI_didDecodeSublist,
        .userdata = NULL,
        .returnString = 0,
        .path = NULL};

    // (gets binary data for json file)
    json_reader reader = {
        .read = (int (*)(void *, uint8_t *, int))read_string,
        .userdata = &text
    };

    int ok = playdate->json->decode(&decoder, reader, out);

    if (!ok)
    {
        free_json_data(*out);
        out->type = kJSONNull;
        return 0;
    }
    return 1;
}