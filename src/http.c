#include "http.h"

#include "utility.h"

#include <string.h>

#define MAX_HTTP 16

static enable_cb_t _cb;
static void* _ud;
char* _domain = NULL;
char* _reason = NULL;
bool skip_enable_check = false;

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
    if (handle == 0)
        return NULL;

    for (int i = 0; i < MAX_HTTP; ++i)
    {
        if (handle_info[i].handle == handle)
            return &handle_info[i];
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

        if (handle_info[i].state == handle_info[best_idx].state &&
            handle_info[i].handle < handle_info[best_idx].handle)
        {
            best_idx = i;
        }
    }

    http_cancel(handle_info[best_idx].handle);
    handle_info[best_idx].handle = next_handle;

    // increment, and ensure never 0.
    ++next_handle;
    if (next_handle == 0)
        ++next_handle;

    return &handle_info[best_idx];
}

static void http_get_(
    struct HTTPUD* httpud, struct HttpHandleInfo* info, const char* domain, const char* path,
    const char* reason, http_result_cb cb, int timeout, void* ud
)
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

    // Immediately unlink to prevent recursion or double-freeing
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
        if (httpud->domain)
            cb_free(httpud->domain);
        if (httpud->path)
            cb_free(httpud->path);

        cb_free(httpud);
    }

    playdate->network->http->close(connection);
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
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);
    if (httpud == NULL)
        return;

    int status = playdate->network->http->getResponseStatus(connection);

    if (httpud->location)
    {
        playdate->system->logToConsole("Redirect detected to: %s", httpud->location);
        httpud->flags |= HTTP_REDIRECT;
    }
}

static void CB_Closed(HTTPConnection* connection)
{
    http_cleanup(connection);
}

static void readAllData(HTTPConnection* connection)
{
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);

    // Handle error callbacks
    if (httpud != NULL && httpud->cb)
    {
        struct HttpHandleInfo* info = get_handle_info(httpud->handle);

        if (httpud->cb)
        {
            int response = playdate->network->http->getResponseStatus(connection);
            if (response != 0 && response != 200)
            {
                if (response == 404)
                {
                    httpud->flags |= HTTP_NOT_FOUND;
                }
                else
                {
                    httpud->flags |= HTTP_NON_SUCCESS_STATUS;
                }

                if (info)
                {
                    info->state = Complete;
                    info->flags = httpud->flags;
                }
                httpud->cb(httpud->flags, NULL, 0, httpud->ud);
                httpud->cb = NULL;
            }
        }
    }

    size_t available;
    while ((available = playdate->network->http->getBytesAvailable(connection)))
    {
        // If redirecting, discard body data to prevent buffer pollution
        if (httpud && (httpud->flags & HTTP_REDIRECT))
        {
            char dummy_buffer[256];
            playdate->network->http->read(
                connection, dummy_buffer,
                (available > sizeof(dummy_buffer)) ? sizeof(dummy_buffer) : available
            );
        }
        else if (httpud && httpud->cb)
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

            if (read > 0)
            {
                httpud->data_len += read;
                httpud->data[httpud->data_len] = 0;
            }
            else if (read <= 0)
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
        }
        else
        {
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

    if (httpud == NULL)
        return;

    struct HttpHandleInfo* info = get_handle_info(httpud->handle);

    playdate->system->logToConsole(
        "Request complete. Flags: %x, DataLen: %d, ContentType: %s", httpud->flags,
        (int)httpud->data_len, httpud->contentType ? httpud->contentType : "NULL"
    );

    http_result_cb saved_cb = httpud->cb;
    void* saved_ud = httpud->ud;
    unsigned saved_flags = httpud->flags;

    char* location_copy = NULL;
    if (httpud->location)
        location_copy = cb_strdup(httpud->location);

    char* data_stolen = NULL;
    size_t data_len = 0;

    bool is_redirect = (saved_flags & HTTP_REDIRECT) && location_copy;
    bool is_html_error = httpud->contentType && strstr(httpud->contentType, "text/html");
    bool is_success = !is_redirect && !is_html_error && httpud->data && httpud->data_len > 0;

    if (is_success)
    {
        data_stolen = httpud->data;
        data_len = httpud->data_len;
        httpud->data = NULL;
        // FIXME: when is this freed?
    }

    if (info)
    {
        info->state = Complete;
        info->flags = saved_flags;
        if (is_html_error)
            info->flags |= HTTP_UNEXPECTED_CONTENT_TYPE;
    }

    httpud->cb = NULL;

    http_cleanup(connection);

    if (saved_cb)
    {
        if (is_redirect)
        {
            saved_cb(saved_flags, location_copy, strlen(location_copy), saved_ud);
        }
        else if (is_html_error)
        {
            saved_cb(saved_flags | HTTP_UNEXPECTED_CONTENT_TYPE, NULL, 0, saved_ud);
        }
        else if (is_success)
        {
            saved_cb(saved_flags, data_stolen, data_len, saved_ud);
        }
        else
        {
            unsigned err_flags = saved_flags;
            if (!(err_flags & (HTTP_ERROR | HTTP_NOT_FOUND | HTTP_CANCELLED)))
            {
                err_flags |= HTTP_ERROR;
            }
            saved_cb(err_flags, NULL, 0, saved_ud);
        }
    }

    if (location_copy)
    {
        cb_free(location_copy);
    }
}

static void CB_Permission(unsigned flags, void* ud)
{
    struct HTTPUD* httpud = ud;
    struct HttpHandleInfo* info = get_handle_info(httpud->handle);

    httpud->flags = flags;
    bool allowed = (flags & ~HTTP_ENABLE_ASKED) == 0;

    if (!info || info->state == Complete)
    {
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

    if (result)
        skip_enable_check = true;
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
        skip_enable_check = true;
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
    if (skip_enable_check)
    {
        cb(0, ud);
        return;
    }

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
    if (!info)
        return;

    // TODO

    switch (info->state)
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
