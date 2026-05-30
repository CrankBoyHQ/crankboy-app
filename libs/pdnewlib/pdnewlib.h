// pdnewlib.h
#pragma once

#include <pd_api.h>

// The main entry point for newlib events from the shim
int eventHandler_pdnewlib(PlaydateAPI* _pd, PDSystemEvent event, uint32_t arg);

#ifdef TARGET_PLAYDATE
// The cleanup function to be called on application exit
void pdnewlib_quit(void);
#endif
