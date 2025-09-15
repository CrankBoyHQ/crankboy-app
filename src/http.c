#include "http.h"

#include "pd_api.h"
#include "utility.h"

#define MAX_HTTP 16

static enable_cb_t _cb;
static void* _ud;
char* _domain = NULL;
char* _reason = NULL;

enum HttpHandleState
{
    Complete,
    Permission,
    Get,
};

struct HTTPUD
{
    http_handle_t handle;
    HTTPConnection* connection;
    http_result_cb cb;
    char* domain;
    char* path;
    char* location;
    char* contentType;
    char* data;
    size_t data_len;
    int timeout;
    unsigned flags;
    void* ud;
};

struct HttpHandleInfo
{
    http_handle_t handle;
    enum HttpHandleState state;
    
    // valid only when state != Complete
    struct HTTPUD* httpud;
    
    // valid only when state == Complete
    unsigned flags;
};

static http_handle_t next_handle = 1;
struct HttpHandleInfo handle_info[MAX_HTTP];

static void CB_Permission(unsigned flags, void* ud);

struct HttpHandleInfo* get_handle_info(http_handle_t handle)
{
    if (handle == 0) return NULL;
    
    for (int i = 0; i < MAX_HTTP; ++i)
    {
        if (handle_info[i].handle == handle) return &handle_info[i];
    }
    
    return NULL;
}

struct HttpHandleInfo* push_handle(void)
{
    int best_idx = 0;
    
    // try to find earliest completed handle
    for (int i = 1; i < MAX_HTTP; ++i)
    {
        if (handle_info[i].state == Complete && handle_info[best_idx].state != Complete)
        {
            best_idx = i;
        }
        
        if (handle_info[i].state == handle_info[best_idx].state && handle_info[i].handle < handle_info[best_idx].handle)
        {
            best_idx = i;
        }
    }
    
    http_cancel(handle_info[best_idx].handle);
    handle_info[best_idx].handle = next_handle;
    
    // increment, and ensure never 0.
    ++next_handle;
    if (next_handle == 0) ++next_handle;
    
    return &handle_info[best_idx];
}

static void http_get_(struct HTTPUD* httpud, struct HttpHandleInfo* info, const char* domain, const char* path, const char* reason, http_result_cb cb, int timeout, void* ud)
{
    info->state = Permission;
    info->httpud = httpud;

    memset(httpud, 0, sizeof(*httpud));
    httpud->handle = info->handle;
    httpud->connection = NULL;
    httpud->cb = cb;
    httpud->ud = ud;
    httpud->timeout = timeout;
    httpud->domain = cb_strdup(domain);
    httpud->path = cb_strdup(path);
    httpud->location = NULL;

    enable_http(domain, reason, CB_Permission, httpud);
}

static void http_cleanup(HTTPConnection* connection)
{
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);
    playdate->network->http->setUserdata(connection, NULL);

    if (httpud)
    {
        if (httpud->cb && !(httpud->flags & HTTP_CANCELLED))
        {
            httpud->flags |= HTTP_ERROR;
        }
        
        struct HttpHandleInfo* info = get_handle_info(httpud->handle);
        if (info)
        {
            info->state = Complete;
            info->flags = httpud->flags;
        }
        
        if (httpud->cb)
        {
            // resort to error if cleanup and cb not yet called
            httpud->cb(httpud->flags, NULL, 0, httpud->ud);
        }

        if (httpud->data)
            cb_free(httpud->data);
        if (httpud->location)
            cb_free(httpud->location);
        if (httpud->contentType)
            cb_free(httpud->contentType);
        cb_free(httpud->domain);
        cb_free(httpud->path);
        cb_free(httpud);
    }

    playdate->network->http->release(connection);
    playdate->network->http->close(connection);
}

// Helper function to parse a full URL into domain and path
static bool parse_url(const char* url, char** domain, char** path)
{
    const char* domain_start = strstr(url, "://");
    if (!domain_start)
    {
        return false;  // Not a full URL
    }
    domain_start += 3;  // Move past "://"

    const char* path_start = strchr(domain_start, '/');
    if (!path_start)
    {
        // URL has no path (e.g., "https://github.com") - unlikely for us
        return false;
    }

    size_t domain_len = path_start - domain_start;
    *domain = cb_malloc(domain_len + 1);
    strncpy(*domain, domain_start, domain_len);
    (*domain)[domain_len] = '\0';

    *path = cb_strdup(path_start);

    return true;
}

static void CB_Header(HTTPConnection* connection, const char* key, const char* value)
{
    playdate->system->logToConsole("Header received: \"%s\": \"%s\"\n", key, value);

    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);
    if (httpud == NULL)
        return;

    if (strcasecmp(key, "Content-Type") == 0)
    {
        httpud->contentType = cb_strdup(value);
    }
    else if (strcasecmp(key, "Location") == 0)
    {
        httpud->location = cb_strdup(value);
    }
}

static void CB_HeadersRead(HTTPConnection* connection)
{
    playdate->system->logToConsole("Headers read\n");
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);
    if (httpud == NULL)
        return;
        
    struct HttpHandleInfo* info = get_handle_info(httpud->handle);

    int status = playdate->network->http->getResponseStatus(connection);

    // Check for redirect status codes (301, 302, 307, etc.)
    if (status >= 300 && status < 400 && httpud->location && info)
    {
        playdate->system->logToConsole("Handling redirect to: %s\n", httpud->location);

        char* new_domain = NULL;
        char* new_path = NULL;

        if (parse_url(httpud->location, &new_domain, &new_path))
        {
            struct HTTPUD* new_httpud = allocz(struct HTTPUD);
            if (new_httpud)
            {
                // Store original request data before cleaning up
                http_result_cb orig_cb = httpud->cb;
                http_handle_t orig_handle = httpud->handle;
                
                // prevent callback on this http request
                httpud->handle = 0;
                httpud->cb = NULL;

                // Start a brand new request with the new URL and original userdata and info
                http_get_(
                    new_httpud, info, new_domain, new_path, "to follow redirect", orig_cb, httpud->timeout, httpud->ud
                );

                cb_free(new_domain);
                cb_free(new_path);
            }
        }
    }
}

static void CB_Closed(HTTPConnection* connection)
{
    http_cleanup(connection);
}

// reads available data, or if status is not 200 then
// reports an error
static void readAllData(HTTPConnection* connection)
{
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);

    // If httpud is null, the connection is being cancelled, but we may still need to drain the
    // buffer.
    if (httpud != NULL)
    {
        struct HttpHandleInfo* info = get_handle_info(httpud->handle);
        
        // Only check status and fire the error callback ONCE.
        // httpud->cb is used as a flag to see if we've already processed an error.
        if (httpud->cb)
        {
            int response = playdate->network->http->getResponseStatus(connection);
            if (response != 0 && response != 200)
            {
                if (response == 404)
                {
                    httpud->flags |= HTTP_NOT_FOUND;
                } else {
                    httpud->flags |= HTTP_NON_SUCCESS_STATUS;
                }
                
                if (info)
                {
                    info->state = Complete;
                    info->flags = httpud->flags;
                }
                httpud->cb(httpud->flags, NULL, 0, httpud->ud);
                    
                // Mark the callback as handled so we don't fire it again.
                httpud->cb = NULL;
            }
        }
    }

    // Unconditionally drain the receive buffer.
    size_t available;
    while ((available = playdate->network->http->getBytesAvailable(connection)))
    {
        // If we are in a success case (httpud and its callback are still valid),
        // read the data into the buffer.
        if (httpud && httpud->cb)
        {
            struct HttpHandleInfo* info = get_handle_info(httpud->handle);
            
            httpud->data = cb_realloc(httpud->data, httpud->data_len + available + 1);
            if (httpud->data == NULL)
            {
                httpud->flags |= HTTP_MEM_ERROR;
                if (info)
                {
                    info->state = Complete;
                    info->flags = httpud->flags;
                }
                httpud->cb(httpud->flags, NULL, 0, httpud->ud);
                httpud->cb = NULL;
                return;
            }
            int read = playdate->network->http->read(
                connection, httpud->data + httpud->data_len, available
            );

            if (read <= 0)
            {
                httpud->flags |= HTTP_ERROR;
                if (info)
                {
                    info->state = Complete;
                    info->flags = httpud->flags;
                }
                httpud->cb(httpud->flags, NULL, 0, httpud->ud);
                httpud->cb = NULL;
                return;
            }

            httpud->data_len += read;
            httpud->data[httpud->data_len] = 0;
        }
        else
        {
            // We are in an error case or are cancelling. Drain the buffer by reading
            // into a temporary sink and discarding the data.
            char dummy_buffer[256];
            playdate->network->http->read(
                connection, dummy_buffer,
                (available > sizeof(dummy_buffer)) ? sizeof(dummy_buffer) : available
            );
        }
    }
}

static void CB_RequestComplete(HTTPConnection* connection)
{
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);

    // If httpud is NULL, it was already cleaned up.
    if (httpud == NULL)
    {
        return;
    }
    
    struct HttpHandleInfo* info = get_handle_info(httpud->handle);
    
    playdate->system->logToConsole("Request complete; contentType: %s", httpud->contentType);

    // This is the final check before we decide to succeed or fail.
    // Check for the HTML error case first.
    // FIXME: why is text/html considered an error?
    if (httpud->contentType && strstr(httpud->contentType, "text/html"))
    {
        if (httpud->cb)
        {
            httpud->flags |= HTTP_UNEXPECTED_CONTENT_TYPE;
            if (info)
            {
                info->state = Complete;
                info->flags = httpud->flags;
            }
            httpud->cb(httpud->flags, NULL, 0, httpud->ud);
            httpud->cb = NULL;
        }
    }
    // If the contentType was okay, check for a successful data download.
    else if (httpud->cb && httpud->data_len > 0 && httpud->data)
    {
        if (info)
        {
            info->state = Complete;
            info->flags = httpud->flags;
        }
        
        httpud->cb(httpud->flags, httpud->data, httpud->data_len, httpud->ud);
        httpud->cb = NULL;
        httpud->data = NULL;
    }

    http_cleanup(connection);
}

static void CB_Permission(unsigned flags, void* ud)
{
    struct HTTPUD* httpud = ud;
    struct HttpHandleInfo* info = get_handle_info(httpud->handle);
    
    httpud->flags = flags;
    bool allowed = (flags & ~HTTP_ENABLE_ASKED) == 0;

    if (!info || info->state == Complete)
    {
        // it's already been cancelled or pre-empted.
        goto fail_no_cb;
    }

    if (allowed)
    {
        playdate->system->logToConsole("http GET: %s%s", httpud->domain, httpud->path);
        httpud->connection = playdate->network->http->newConnection(httpud->domain, 0, USE_SSL);
        HTTPConnection* connection = httpud->connection;
        
        if (!connection)
            goto fail;

        info->state = Get;
        playdate->network->http->setUserdata(connection, httpud);
        playdate->network->http->retain(connection);

        playdate->network->http->setHeaderReceivedCallback(connection, CB_Header);
        playdate->network->http->setHeadersReadCallback(connection, CB_HeadersRead);
        playdate->network->http->setConnectionClosedCallback(connection, CB_Closed);
        playdate->network->http->setResponseCallback(connection, readAllData);
        playdate->network->http->setRequestCompleteCallback(connection, CB_RequestComplete);
        playdate->network->http->setConnectTimeout(connection, httpud->timeout);

        PDNetErr err = playdate->network->http->get(connection, httpud->path, NULL, 0);
        if (err != NET_OK)
        {
            flags |= HTTP_ERROR;
            goto release_and_fail;
        }

        playdate->system->logToConsole("HTTP get, no immediate error\n");
        return;

    release_and_fail:
        info->state = Complete;
        info->flags = httpud->flags;
        httpud->cb(flags, NULL, 0, httpud->ud);
        httpud->cb = NULL;
        http_cleanup(connection);
    }
    else
    {
    fail:
        if (info)
        {
            info->state = Complete;
            info->flags = flags;
        }
        httpud->cb(flags, NULL, 0, httpud->ud);
    fail_no_cb:
        httpud->cb = NULL;
        if (httpud->data)
            cb_free(httpud->data);
        if (httpud->contentType)
            cb_free(httpud->contentType);
        cb_free(httpud->domain);
        cb_free(httpud->path);
        cb_free(httpud);
    }
}

http_handle_t http_get(
    const char* domain, const char* path, const char* reason, http_result_cb cb, int timeout,
    void* ud
)
{
    struct HTTPUD* httpud = cb_malloc(sizeof(struct HTTPUD));
    if (!httpud)
    {
        cb(HTTP_MEM_ERROR, NULL, 0, ud);
        return 0;
    }
    
    struct HttpHandleInfo* info = push_handle();
    CB_ASSERT(info);
    
    http_get_(httpud, info, domain, path, reason, cb, timeout, ud);
    
    return info->handle;
}

struct CB_UserData_EnableHTTP
{
    enable_cb_t cb;
    void* ud;
};

static void CB_SetEnabled(PDNetErr err);

static void CB_CheckWifiStatus(PDNetErr err)
{
    WifiStatus status = playdate->network->getStatus();

    if (status == kWifiNotAvailable)
    {
        enable_cb_t cb = _cb;
        void* ud = _ud;
        _cb = NULL;

        playdate->system->logToConsole("WIFI not available, aborting HTTP request.");

        cb_free(_domain);
        _domain = NULL;
        cb_free(_reason);
        _reason = NULL;

        if (cb)
        {
            cb(HTTP_WIFI_NOT_AVAILABLE, ud);
        }
    }
    else
    {
        CB_SetEnabled(err);
    }
}

static void CB_AccessReply(bool result, void* cbud)
{
    enable_cb_t cb = ((struct CB_UserData_EnableHTTP*)cbud)->cb;
    void* ud = ((struct CB_UserData_EnableHTTP*)cbud)->ud;
    cb_free(cbud);

    cb(HTTP_ENABLE_ASKED | (result ? 0 : HTTP_ENABLE_DENIED), ud);

    cb_free(_domain);
    _domain = NULL;
    cb_free(_reason);
    _reason = NULL;
}

static void CB_SetEnabled(PDNetErr err)
{
    enable_cb_t cb = _cb;
    void* ud = _ud;
    _cb = NULL;

    if (err != NET_OK)
    {
        cb(HTTP_ERROR, ud);
        cb_free(_domain);
        _domain = NULL;
        cb_free(_reason);
        _reason = NULL;
        return;
    }

    struct CB_UserData_EnableHTTP* cbudhttp = cb_malloc(sizeof(struct CB_UserData_EnableHTTP));
    if (!cbudhttp)
    {
        cb(HTTP_MEM_ERROR, ud);
        cb_free(_domain);
        _domain = NULL;
        cb_free(_reason);
        _reason = NULL;
        return;
    }

    cbudhttp->cb = cb;
    cbudhttp->ud = ud;

    enum accessReply result = playdate->network->http->requestAccess(
        _domain, 0, USE_SSL, _reason, CB_AccessReply, cbudhttp
    );

    switch (result)
    {
    case kAccessAsk:
        playdate->system->logToConsole("Asked for permission\n");
        // callback will be invoked.
        break;

    case kAccessDeny:
        cb_free(cbudhttp);
        cb(HTTP_ENABLE_DENIED, ud);
        cb_free(_domain);
        _domain = NULL;
        cb_free(_reason);
        _reason = NULL;
        break;

    case kAccessAllow:
        cb_free(cbudhttp);
        cb(0, ud);
        cb_free(_domain);
        _domain = NULL;
        cb_free(_reason);
        _reason = NULL;
        break;

    default:
        cb_free(cbudhttp);
        playdate->system->logToConsole("Unrecognized permission result: %d\n", result);
        cb(HTTP_ERROR, ud);
        cb_free(_domain);
        _domain = NULL;
        cb_free(_reason);
        _reason = NULL;
        break;
    }
}

void enable_http(const char* domain, const char* reason, enable_cb_t cb, void* ud)
{
    if (_cb != NULL)
    {
        cb(HTTP_ENABLE_IN_PROGRESS, ud);
        return;
    }

    _ud = ud;
    _cb = cb;
    _domain = cb_strdup(domain);
    _reason = cb_strdup(reason);

    playdate->network->setEnabled(true, CB_CheckWifiStatus);
}

void http_cancel(http_handle_t handle)
{
    struct HttpHandleInfo* info = get_handle_info(handle);
    if (!info) return;
    
    // TODO
    
    switch(info->state)
    {
    case Permission:
        info->state = Complete;
        info->flags = HTTP_CANCELLED;
        if (info->httpud && info->httpud->cb)
        {
            info->httpud->cb(info->flags, NULL, 0, info->httpud->ud);
            // httpud will be cleaned up later, in CB_Permission()
        }
        break;
    case Get:
        info->state = Complete;
        info->httpud->flags |= HTTP_CANCELLED;
        info->flags = info->httpud->flags;
        http_cleanup(info->httpud->connection);
        break;
    default:
    case Complete:
        break;
    }
}