#pragma once

// returns 0 on failure
// if result is provided, *result is set to an error string if failure occurred
// the error string must be freed by the caller
int check_for_updates(void, char** result);