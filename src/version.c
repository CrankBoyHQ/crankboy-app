#include "version.h"
#include "pd_api.h"
#include "utility.h"
#include "jparse.h"

static void callback(PDNetErr err)
{
    
}

struct VersionInfo
{
    char* local_version;
    char* domain;
    char* url;
};

static struct VersionInfo localVersionInfo;

int read_local_version_info()
{
    if (localVersionInfo.local_version == NULL)
    {
        
    }
}

int check_for_updates(void, char** result)
{
    if (result)
        *result = NULL;
    
    struct json_value local_version_info;
    local_version_info.type = kJSONNull;
    
    int jparse_result = parse_json("version.json", &local_version_info);
    if (jparse_result == 0 || local_version_info.type != kJSONTable)
    {
    local_version_parse_error:
        if (result)
            *result = stdup("Failed to determine current version info for reference");
        free_json_data(local_version_info);
        return 0;
    }
    
    // read info json
    JsonObject* obj = local_version_info.data.tableval;
    for (size_t i = 0; i < obj->n; ++i)
    {
    }
    
    playdate->network->setEnabled(false, callback);
    
    enum accessReply result = playdate->network->http->requestAccess(const char* server, int port, bool usessl, const char* purpose, AccessRequestCallback* requestCallback, void* userdata);
}