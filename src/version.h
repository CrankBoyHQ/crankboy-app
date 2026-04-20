#pragma once

#ifndef GITHUB_RELEASE
#define GITHUB_RELEASE 0
#endif

#define ERR_PERMISSION_ASKED_DENIED (-253)
#define ERR_PERMISSION_DENIED (-254)

typedef struct
{
    char* version;
    char* url;
} PendingUpdateInfo;

// Checks for updates if it's been more than a certain amount of time since the last check.
void possibly_check_for_updates(void);

// Reads update status file and returns info if an update modal should be shown.
PendingUpdateInfo* get_pending_update(void);

void free_pending_update_info(PendingUpdateInfo* info);

// Updates the status file to indicate the user has seen the notification.
void mark_update_as_seen(void);

void version_quit(void);

const char* get_current_version(void);
const char* get_download_url(void);
