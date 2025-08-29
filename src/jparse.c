#include "jparse.h"

#include "pd_api.h"
#include "utility.h"

__section__(".rare") void SI_willDecodeSublist(
    json_decoder* decoder, const char* name, json_value_type type
)
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

__section__(".rare") void SI_didDecodeArrayValue(json_decoder* decoder, int pos, json_value value)
{
    --pos;  // one-indexed (!!)
    JsonArray* array = decoder->userdata;
    int n = array ? array->n : 0;
    if (pos >= n)
        n = pos + 1;
    size_t p2n = next_pow2(n);

    array = playdate->system->realloc(array, sizeof(JsonArray) + p2n * sizeof(json_value));

    if (value.type == kJSONString)
    {
        // we need to own the string
        value.data.stringval = cb_strdup(value.data.stringval);
    }

    array->data[pos] = value;
    array->n = n;
    decoder->userdata = array;
    return;
}

__section__(".rare") void SI_didDecodeTableValue(
    json_decoder* decoder, const char* key, json_value value
)
{
    JsonObject* obj = decoder->userdata;

    int n = 1 + (obj ? obj->n : 0);

    size_t p2n = next_pow2(n);

    obj = playdate->system->realloc(obj, sizeof(JsonObject) + p2n * sizeof(TableKeyPair));

    if (value.type == kJSONString)
    {
        // we need to own the string
        value.data.stringval = cb_strdup(value.data.stringval);
    }

    obj->data[n - 1].key = cb_strdup(key);
    obj->data[n - 1].value = value;
    obj->n = n;
    decoder->userdata = obj;
    return;
}

__section__(".rare") void* SI_didDecodeSublist(
    json_decoder* decoder, const char* name, json_value_type type
)
{
    return decoder->userdata;
}

__section__(".rare") void free_json_data(json_value v)
{
    if (v.type == kJSONArray)
    {
        JsonArray* array = (JsonArray*)v.data.arrayval;
        for (size_t i = 0; i < array->n; i++)
        {
            free_json_data(array->data[i]);
        }
        cb_free(array);
    }
    else if (v.type == kJSONTable)
    {
        JsonObject* obj = (JsonObject*)v.data.tableval;
        for (size_t i = 0; i < obj->n; i++)
        {
            cb_free(obj->data[i].key);
            free_json_data(obj->data[i].value);
        }
        cb_free(obj);
    }
    else if (v.type == kJSONString)
    {
        cb_free(v.data.stringval);
    }
}

__section__(".rare") static void decodeError(
    struct json_decoder* decoder, const char* error, int linenum
)
{
    playdate->system->logToConsole("Error decoding json: %s", error);
}

json_value json_new_table(void)
{
    json_value v;
    v.data.tableval = allocz(JsonObject);
    if (!v.data.tableval)
    {
        v.type = kJSONNull;
    }
    else
    {
        v.type = kJSONTable;
    }

    return v;
}

json_value json_new_string(const char* s)
{
    json_value jv = {.type = kJSONString};
    jv.data.stringval = aprintf("%s", s);
    return jv;
}

json_value json_new_bool(bool v)
{
    json_value jv = {.type = v ? kJSONTrue : kJSONFalse};
    return jv;
}

json_value json_new_int(int i)
{
    json_value jv = {
        .type = kJSONInteger,
    };
    jv.data.intval = i;
    return jv;
}

bool json_set_table_value(json_value* table, const char* key, json_value value)
{
    if (table->type != kJSONTable)
        return false;

    JsonObject* obj = table->data.tableval;

    // check for existing matching key
    for (size_t i = 0; i < obj->n; ++i)
    {
        if (!strcmp(obj->data[i].key, key))
        {
            free_json_data(obj->data[i].value);
            obj->data[i].value = value;
            goto done;
        }
    }

    char* key2 = cb_strdup(key);
    if (!key2)
        return false;

    // add new key
    obj = cb_realloc(obj, sizeof(*obj) + sizeof(obj->data[0]) * (obj->n + 1));
    if (!obj)
    {
        cb_free(key2);
        return false;
    }

    obj->data[obj->n].value = value;
    obj->data[obj->n++].key = key2;

done:
    table->data.tableval = obj;
    return true;
}

__section__(".rare") int parse_json(const char* path, json_value* out, FileOptions opts)
{
    if (!out)
        return 0;
    out->type = kJSONNull;

    SDFile* file = playdate->file->open(path, opts);
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
        .path = NULL
    };

    // (gets binary data for json file)
    json_reader reader = {
        .read = (int (*)(void*, uint8_t*, int))playdate->file->read, .userdata = file
    };

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

__section__(".rare") void encode_json(json_encoder* e, json_value j)
{
    switch (j.type)
    {
    case kJSONNull:
        e->writeNull(e);
        break;
    case kJSONFalse:
        e->writeFalse(e);
        break;
    case kJSONTrue:
        e->writeTrue(e);
        break;
    case kJSONInteger:
        e->writeInt(e, j.data.intval);
        break;
    case kJSONFloat:
        e->writeDouble(e, j.data.floatval);
        break;
    case kJSONString:
        e->writeString(e, j.data.stringval, strlen(j.data.stringval));
        break;
    case kJSONTable:
    {
        e->startTable(e);
        JsonObject* obj = j.data.tableval;
        for (size_t i = 0; i < obj->n; ++i)
        {
            e->addTableMember(e, obj->data[i].key, strlen(obj->data[i].key));
            encode_json(e, obj->data[i].value);
        }
        e->endTable(e);
    }
    break;
    case kJSONArray:
        e->startArray(e);
        JsonArray* obj = j.data.arrayval;
        for (size_t i = 0; i < obj->n; ++i)
        {
            e->addArrayMember(e);
            encode_json(e, obj->data[i]);
        }
        e->endArray(e);
    default:
        return;
    }
}

__section__(".rare") static void writefile(void* userdata, const char* str, int len)
{
    playdate->file->write((SDFile*)userdata, str, len);
}

__section__(".rare") int write_json_to_disk(const char* path, json_value out)
{
    json_encoder encoder;

    SDFile* file = playdate->file->open(path, kFileWrite);
    if (!file)
        return -1;

    playdate->json->initEncoder(&encoder, writefile, file, 1);
    encode_json(&encoder, out);
    playdate->file->close(file);
    return 0;
}

__section__(".rare") json_value json_get_table_value(json_value j, const char* key)
{
    if (j.type != kJSONTable)
        goto ret_null;
    if (!key)
        goto ret_null;
    JsonObject* obj = j.data.tableval;
    if (!obj)
        goto ret_null;

    for (size_t i = 0; i < obj->n; ++i)
    {
        if (!strcmp(obj->data[i].key, key))
        {
            return obj->data[i].value;
        }
    }

ret_null:
    j.type = kJSONNull;
    return j;
}

__section__(".rare") int read_string(const char** text, uint8_t* out, int bufsize)
{
    int maxlen = strnlen(*text, bufsize);
    if (maxlen == 0)
        return 0;
    memcpy(out, *text, maxlen);
    *text += maxlen;
    return maxlen;
}

__section__(".rare") int compare_key_pairs(const void* a, const void* b)
{
    const TableKeyPair* pair_a = (const TableKeyPair*)a;
    const TableKeyPair* pair_b = (const TableKeyPair*)b;
    return strcmp(pair_a->key, pair_b->key);
}

__section__(".rare") int parse_json_string(const char* text, json_value* out)
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
        .path = NULL
    };

    // (gets binary data for json file)
    json_reader reader = {.read = (int (*)(void*, uint8_t*, int))read_string, .userdata = &text};

    int ok = playdate->json->decode(&decoder, reader, out);

    if (!ok)
    {
        free_json_data(*out);
        out->type = kJSONNull;
        return 0;
    }
    return 1;
}
