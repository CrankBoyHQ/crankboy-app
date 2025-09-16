#pragma once

#include "pd_api.h"

#include <stdint.h>

typedef struct TableKeyPair
{
    char* key;
    json_value value;
} TableKeyPair;

typedef struct JsonObject
{
    size_t n;
    TableKeyPair data[];
} JsonObject;

typedef struct JsonArray
{
    size_t n;
    json_value data[];
} JsonArray;

void free_json_data(json_value);

// returns 0 on failure
// opts should be kFileRead, kFileReadData, or kFileRead | kFileReadData
int parse_json(const char* file, json_value* out, FileOptions opts);

// returns 0 on failure
int parse_json_string(const char* text, json_value* out);

// returns 0 on success
int write_json_to_disk(const char* path, json_value out);

int compare_key_pairs(const void* a, const void* b);

json_value json_get_table_value(json_value table, const char* key);

json_value json_new_table(void);

json_value json_new_string(const char* s);
json_value json_new_bool(bool v);
json_value json_new_int(int v);

// returns NULL if not a string.
const char* json_as_string(json_value j);

// returns true on success
bool json_set_table_value(json_value* table, const char* key, json_value value);
