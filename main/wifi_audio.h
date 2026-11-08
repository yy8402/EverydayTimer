#pragma once

#include "cJSON.h"

namespace wifi_audio {
void init();
void voice_button();
const char *configure(const cJSON *request);
const char *pair(const cJSON *request);
const char *status();
}
