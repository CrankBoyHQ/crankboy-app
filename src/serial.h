#pragma once

#include <stdint.h>

void CB_on_serial_message(const char* data);

// Timestamp (ms) of the last received serial message, 0 if none received yet.
uint32_t serial_get_last_activity_ms(void);