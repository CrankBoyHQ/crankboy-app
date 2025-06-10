#pragma once

#include <stdint.h>

#include "pd_api.h"

typedef struct TableKeyPair
{
    char *key;
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
int parse_json(const char *file, json_value *out);
int parse_json_string(const char *text, json_value *out);