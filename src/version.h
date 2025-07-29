#pragma once

#include "http.h"

#define ERR_PERMISSION_ASKED_DENIED (-253)
#define ERR_PERMISSION_DENIED (-254)

// any negative code is considered an error
// 0: success (but it's the same as our current version)
// 1: success (but it's a version we were already aware of from a previous check)
// 2: success (and we didn't know about this version before)
typedef void (*update_result_cb)(int code, const char* text, void* ud);

void check_for_updates(update_result_cb cb, void* ud);

// check for updates if it's been more than a certain amount of time since we last checked
void possibly_check_for_updates(update_result_cb cb, void* ud);

void version_update_notification_shown(const char* version_name);

void version_quit(void);

const char* get_current_version(void);
const char* get_download_url(void);
