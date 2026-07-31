#pragma once

#include "http.h"

#include <stdbool.h>

// like standard http functions, but ensures http_cancel is never used,
// as that function seems to be unsafe somehow when dispatching many HTTP requests in short
// succession.

typedef struct HTTPSafe
{
    http_handle_t handle;
    http_result_cb cb;
    void* ud;

    bool enqueued;
    bool tombstone;  // slate for deletion
    // bool queued_https;

    struct HTTPQueued
    {
        char* domain;
        char* path;
        char* reason;
        http_result_cb cb;
        void* ud;
        unsigned timeout_ms;
    } queued;
} HTTPSafe;

HTTPSafe* http_safe_new(void);

// Frees the safe. If a request is in flight, the safe is tombstoned and
// released when the request completes; the pending user callback is then
// NEVER invoked (its ud is presumed stale). Use http_safe_ud() beforehand
// to reclaim any caller-allocated userdata.
void http_safe_free(HTTPSafe* safe);

// Returns the userdata pointer of the current request (NULL if none).
void* http_safe_ud(HTTPSafe* safe);

void http_safe_replace_get(
    HTTPSafe* safe, const char* domain, const char* path, const char* reason, http_result_cb cb,
    int timeout_ms, void* ud
);

void http_safe_cancel(HTTPSafe* safe);

bool http_safe_in_progress(HTTPSafe* safe);
