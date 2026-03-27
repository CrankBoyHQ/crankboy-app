#include "http_safe.h"

#include "utility.h"

#include <string.h>

// Helper to parse the redirect URL
static bool parse_url_safe(const char* url, char** domain, char** path)
{
    const char* domain_start = strstr(url, "://");
    if (!domain_start)
        return false;
    domain_start += 3;

    const char* path_start = strchr(domain_start, '/');
    if (!path_start)
        return false;

    size_t domain_len = path_start - domain_start;
    *domain = cb_malloc(domain_len + 1);
    strncpy(*domain, domain_start, domain_len);
    (*domain)[domain_len] = '\0';

    *path = cb_strdup(path_start);
    return true;
}

// allow network stack to flush
static void busy_wait(float seconds)
{
    int ms = (int)(seconds * 1000.0f);
    int start = playdate->system->getCurrentTimeMilliseconds();
    while (playdate->system->getCurrentTimeMilliseconds() - start < ms)
    {
    }
}

HTTPSafe* http_safe_new(void)
{
    return allocz(HTTPSafe);
}

void http_safe_free(HTTPSafe* safe)
{
    if (safe->handle == 0)
    {
        free(safe);
    }
    else
    {
        safe->tombstone = true;
    }
}

void http_safe_cb(unsigned flags, char* data, size_t data_len, HTTPSafe* safe)
{
    http_result_cb cb = safe->cb;
    void* ud = safe->ud;

    if (flags & HTTP_REDIRECT)
    {
        playdate->system->logToConsole("HTTPSafe: Catching redirect to %s", data ? data : "(null)");

        char* new_domain = NULL;
        char* new_path = NULL;

        if (data && parse_url_safe(data, &new_domain, &new_path))
        {
            safe->handle = 0;

            const char* reason = safe->queued.reason ? safe->queued.reason : "following redirect";

            playdate->system->logToConsole("HTTPSafe: Waiting 200ms to flush network stack...");
            busy_wait(0.2f);

            http_safe_replace_get(safe, new_domain, new_path, reason, cb, 15000, ud);

            cb_free(new_domain);
            cb_free(new_path);
            return;
        }
        else
        {
            playdate->system->logToConsole("HTTPSafe: Failed to parse redirect URL");
        }
    }

    if (cb == NULL)
    {
        if (safe->tombstone && safe->handle == 0)
        {
            cb_free(safe);
        }
        return;
    }

    safe->handle = 0;
    safe->cb = NULL;
    safe->ud = NULL;

    if (!safe->enqueued && !safe->tombstone)
    {
        cb(flags, data, data_len, ud);
    }
    else
    {
        playdate->system->logToConsole("HTTPSafe: pre-empted");

        if (safe->queued.cb != NULL)
        {
            struct HTTPQueued q = safe->queued;

            safe->enqueued = false;
            memset(&safe->queued, 0, sizeof(safe->queued));

            if (!safe->tombstone)
            {
                http_safe_replace_get(safe, q.domain, q.path, q.reason, q.cb, q.timeout_ms, q.ud);
            }

            cb(HTTP_CANCELLED, NULL, 0, ud);

            cb_free(q.domain);
            cb_free(q.path);
            cb_free(q.reason);
        }
        else
        {
            safe->enqueued = false;
            cb(HTTP_CANCELLED, NULL, 0, ud);
        }

        if (safe->tombstone)
        {
            cb_free(safe);
        }
    }
}

void http_safe_replace_get(
    HTTPSafe* safe, const char* domain, const char* path, const char* reason, http_result_cb cb,
    int timeout_ms, void* ud
)
{
    if (safe->handle == 0)
    {
        safe->cb = cb;
        safe->ud = ud;

        safe->handle = http_get(domain, path, reason, (void*)http_safe_cb, timeout_ms, safe);
    }
    else
    {
        safe->enqueued = true;

        safe->queued.cb = cb;
        safe->queued.ud = ud;
        safe->queued.timeout_ms = timeout_ms;

        safe->queued.domain = cb_strdup(domain);
        safe->queued.path = cb_strdup(path);
        safe->queued.reason = cb_strdup(reason);
    }
}

void http_safe_cancel(HTTPSafe* safe)
{
    if (safe->handle)
    {
        http_cancel(safe->handle);
        safe->handle = 0;
    }
    safe->enqueued = false;
    safe->cb = NULL;
    safe->ud = NULL;

    // Clear any queued request
    if (safe->queued.domain)
    {
        cb_free(safe->queued.domain);
        safe->queued.domain = NULL;
    }
    if (safe->queued.path)
    {
        cb_free(safe->queued.path);
        safe->queued.path = NULL;
    }
    if (safe->queued.reason)
    {
        cb_free(safe->queued.reason);
        safe->queued.reason = NULL;
    }
    safe->queued.cb = NULL;
    safe->queued.ud = NULL;
}

bool http_safe_in_progress(HTTPSafe* safe)
{
    if (safe->enqueued || safe->handle)
        return true;
    return false;
}
