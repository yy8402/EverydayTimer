#pragma once
#include "esp_err.h"
#include <string>

namespace activity_drive {
using ReadActivities = std::string (*)();
using ApplyActivities = void (*)(const std::string &original,
                                 std::string edited);
using Mounted = void (*)();
using ReportError = void (*)(const char *message);

esp_err_t init(ReadActivities read, ApplyActivities apply, ReportError report,
               Mounted mounted);
std::string id_for_name(const std::string &name);
} // namespace activity_drive
