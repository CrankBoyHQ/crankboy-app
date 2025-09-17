#pragma once

#include "http.h"

// like standard http functions, but ensures http_cancel is never used,
// as that function seems to be unsafe somehow when dispatching many HTTP requests in short succession.

typedef struct HTTPSafe
{
    http_handle_t handle;
    http_result_cb cb;
    void* ud;
    
    bool enqueued;
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

void http_safe_replace_get(
    HTTPSafe* safe, const char* domain, const char* path, const char* reason, http_result_cb cb, int timeout_ms, void* ud
);

void http_safe_cancel(HTTPSafe* safe);

bool http_safe_in_progress(HTTPSafe* safe);