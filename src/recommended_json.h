#pragma once

struct ScriptRecommendedSettings;

void recommended_json_init(void);
const struct ScriptRecommendedSettings* recommended_json_lookup(const char* rom_name);
void recommended_json_quit(void);
