#include "http.h"

#include "pd_api.h"
#include "utility.h"

static enable_cb_t _cb;
static void* _ud;
char* _domain = NULL;
char* _reason = NULL;

struct HTTPUD
{
    HTTPConnection** out_connection_handle;
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

static void http_cleanup(HTTPConnection* connection)
{
    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);
    playdate->network->http->setUserdata(connection, NULL);

    if (httpud)
    {
        if (httpud->cb)
        {
            // resort to error if cleanup and cb not yet called
            httpud->cb(HTTP_ERROR | httpud->flags, NULL, 0, httpud->ud);
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

    int status = playdate->network->http->getResponseStatus(connection);

    // Check for redirect status codes (301, 302, 307, etc.)
    if (status >= 300 && status < 400 && httpud->location)
    {
        playdate->system->logToConsole("Handling redirect to: %s\n", httpud->location);

        char* new_domain = NULL;
        char* new_path = NULL;

        if (parse_url(httpud->location, &new_domain, &new_path))
        {
            // Store original request data before cleaning up
            http_result_cb orig_cb = httpud->cb;
            void* orig_ud = httpud->ud;
            int orig_timeout = httpud->timeout;
            unsigned orig_flags = httpud->flags;

            // Mark the current request's callback as NULL so it doesn't fire an error on cleanup
            httpud->cb = NULL;

            // Start a brand new request with the new URL and original userdata
            http_get(
                new_domain, new_path, "following redirect", orig_cb, orig_timeout, orig_ud,
                httpud->out_connection_handle  // Pass the original handle pointer along
            );

            cb_free(new_domain);
            cb_free(new_path);
        }

        http_cleanup(connection);
        return;
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
        // Only check status and fire the error callback ONCE.
        // httpud->cb is used as a flag to see if we've already processed an error.
        if (httpud->cb)
        {
            int response = playdate->network->http->getResponseStatus(connection);
            if (response != 0 && response != 200)
            {
                if (response == 404)
                {
                    httpud->cb(HTTP_NOT_FOUND | httpud->flags, NULL, 0, httpud->ud);
                }
                else
                {
                    httpud->cb(HTTP_NON_SUCCESS_STATUS | httpud->flags, NULL, 0, httpud->ud);
                }
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
            httpud->data = cb_realloc(httpud->data, httpud->data_len + available + 1);
            if (httpud->data == NULL)
            {
                httpud->cb(HTTP_MEM_ERROR | httpud->flags, NULL, 0, httpud->ud);
                httpud->cb = NULL;
                return;
            }
            int read = playdate->network->http->read(
                connection, httpud->data + httpud->data_len, available
            );

            if (read <= 0)
            {
                httpud->cb(HTTP_ERROR | httpud->flags, NULL, 0, httpud->ud);
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

    // This is the final check before we decide to succeed or fail.
    // Check for the HTML error case first.
    if (httpud->contentType && strstr(httpud->contentType, "text/html"))
    {
        if (httpud->cb)
        {
            httpud->cb(HTTP_UNEXPECTED_CONTENT_TYPE | httpud->flags, NULL, 0, httpud->ud);
            httpud->cb = NULL;
        }
    }
    // If the contentType was okay, check for a successful data download.
    else if (httpud->cb && httpud->data_len > 0 && httpud->data)
    {
        httpud->cb(httpud->flags, httpud->data, httpud->data_len, httpud->ud);
        httpud->cb = NULL;
        httpud->data = NULL;
    }

    http_cleanup(connection);
}

static void CB_Permission(unsigned flags, void* ud)
{
    struct HTTPUD* httpud = ud;

    httpud->flags = flags;
    bool allowed = (flags & ~HTTP_ENABLE_ASKED) == 0;

    if (allowed)
    {
        httpud->connection = playdate->network->http->newConnection(httpud->domain, 0, USE_SSL);
        HTTPConnection* connection = httpud->connection;

        if (!connection)
            goto fail;

        if (httpud->out_connection_handle)
        {
            *(httpud->out_connection_handle) = connection;
        }

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
        httpud->cb(flags, NULL, 0, httpud->ud);
        httpud->cb = NULL;
        http_cleanup(connection);
    }
    else
    {
    fail:
        httpud->cb(flags, NULL, 0, httpud->ud);
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

void http_get(
    const char* domain, const char* path, const char* reason, http_result_cb cb, int timeout,
    void* ud, HTTPConnection** out_connection_handle
)
{
    struct HTTPUD* httpud = cb_malloc(sizeof(struct HTTPUD));
    if (!httpud)
    {
        cb(HTTP_MEM_ERROR, NULL, 0, ud);
        return;
    }

    if (out_connection_handle)
    {
        *out_connection_handle = NULL;  // Clear the handle pointer immediately
    }

    memset(httpud, 0, sizeof(*httpud));
    httpud->connection = NULL;
    httpud->cb = cb;
    httpud->ud = ud;
    httpud->timeout = timeout;
    httpud->domain = cb_strdup(domain);
    httpud->path = cb_strdup(path);
    httpud->location = NULL;
    httpud->out_connection_handle = out_connection_handle;

    enable_http(domain, reason, CB_Permission, httpud);
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

void http_cancel_and_cleanup(HTTPConnection* connection)
{
    if (connection == NULL)
    {
        return;
    }

    struct HTTPUD* httpud = playdate->network->http->getUserdata(connection);
    if (httpud)
    {
        if (httpud->ud)
        {
            cb_free(httpud->ud);
            httpud->ud = NULL;
        }

        httpud->cb = NULL;
    }

    playdate->network->http->close(connection);
}
