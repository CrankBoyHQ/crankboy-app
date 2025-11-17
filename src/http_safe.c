#include "http_safe.h"

#include "utility.h"

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
    safe->enqueued = true;
}

bool http_safe_in_progress(HTTPSafe* safe)
{
    if (safe->enqueued || safe->handle)
        return true;
    return false;
}
