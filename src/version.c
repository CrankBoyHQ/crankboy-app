#include "pd_api.h"

#include "version.h"
#include "utility.h"
#include "jparse.h"

struct VersionInfo
{
    char* name;
    char* domain;
    char* url;
};

static struct VersionInfo* localVersionInfo = NULL;

void version_info_free(struct VersionInfo* out)
{
    if (!out) return;
    if (out->name) free(out->name);
    if (out->domain) free(out->domain);
    if (out->url) free(out->url);
    free(out);
}

static struct VersionInfo* version_from_json(json_value j)
{
    if (j.type != kJSONTable) return NULL;
    
    struct VersionInfo* out = malloc(sizeof(struct VersionInfo));
    memset(out, 0, sizeof(*out));
    
    JsonObject* obj = j.data.tableval;
    for (size_t i = 0; i < obj->n; ++i)
    {
        if (obj->data[i].value.type == kJSONString)
        {
            if (strcmp(obj->data[i].key, "name") == 0)
            {
                out->name = strdup(obj->data[i].key);
            }
            else if (strcmp(obj->data[i].key, "domain") == 0)
            {
                out->domain = strdup(obj->data[i].key);
            }
            if (strcmp(obj->data[i].key, "url") == 0)
            {
                out->url = strdup(obj->data[i].key);
            }
        }
    }
    
    return out;
}

void read_local_version_info(void)
{
    if (localVersionInfo == NULL)
    {
        json_value local_version_info;
        int jparse_result = parse_json("version.json", &local_version_info);
        if (jparse_result != 0)
        {
            localVersionInfo = version_from_json(local_version_info);
            free_json_data(local_version_info);
        }
    }
}

void permission_callback(bool allowed, void* userdata)
{
    printf("allowed: %d\n", (int)allowed);
}

int check_for_updates(char** result)
{
    if (result)
        *result = NULL;
    
    json_value local_version_info;
    local_version_info.type = kJSONNull;
    
    read_local_version_info();
    
    if (!localVersionInfo || !localVersionInfo->name || !localVersionInfo->domain || !localVersionInfo->url) return 0;
    
    playdate->network->setEnabled(false, NULL);
    
    enum accessReply reply = playdate->network->http->requestAccess(
        localVersionInfo->domain, 80, false, "Check for updates",
        permission_callback, NULL
    );
    
    printf("request result: %d\n", reply);
    return 1;
}