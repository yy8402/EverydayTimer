#pragma once
#include "esp_err.h"
#include <string>
#include <cstdint>
#include <cstdio>

namespace activity_drive {
using ReadActivities = std::string (*)();
using ApplyActivities = void (*)(const std::string &original,
                                 std::string edited);
using ReportError = void (*)(const char *message);

using ExportHistory = bool (*)(FILE *output, uint64_t &revision);
using ClearHistory = bool (*)(uint64_t revision);

esp_err_t init(ReadActivities read, ApplyActivities apply, ReportError report,
               ExportHistory export_csv, ClearHistory clear);
std::string id_for_name(const std::string &name);
} // namespace activity_drive
