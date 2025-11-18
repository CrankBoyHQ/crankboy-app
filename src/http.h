#pragma once

#include "pd_api.h"

#include <stdio.h>

#define USE_SSL true

// all of these indicate failure except for HTTP_ENABLE_ASKED and possibly
// HTTP_UNEXPECTED_CONTENT_TYPE.
#define HTTP_ENABLE_DENIED 1
#define HTTP_ENABLE_ASKED 2 /* does not indicate failure */
#define HTTP_ENABLE_IN_PROGRESS 4
#define HTTP_ERROR 8
#define HTTP_MEM_ERROR 16
#define HTTP_TIMEOUT 32
#define HTTP_NON_SUCCESS_STATUS 64
#define HTTP_UNEXPECTED_CONTENT_TYPE 128
#define HTTP_NOT_FOUND 256
#define HTTP_WIFI_NOT_AVAILABLE 512
#define HTTP_CANCELLED 1024
#define HTTP_REDIRECT 2048

typedef void (*enable_cb_t)(unsigned flags, void* ud);

typedef uint32_t http_handle_t;

// attempts to enable HTTP, then invokes the given callback
void enable_http(const char* domain, const char* reason, enable_cb_t cb, void* ud);

typedef void (*http_result_cb)(unsigned flags, char* data, size_t data_len, void* ud);

// performs an HTTP request, then invokes the callback.
// automatically does enable_http as part of this.
// if cb is required, it's guaranteed that cb will eventually be called
// (unless the entire program terminates first.)
// return result will never be 0, except in the case of a memory error.
http_handle_t http_get(
    const char* domain, const char* path, const char* reason, http_result_cb cb, int timeout_ms,
    void* ud
);

// Manually cancels the given HTTP connection, if it has not already completed.
// cb is invoked with error code HTTP_CANCELLED
void http_cancel(http_handle_t);
