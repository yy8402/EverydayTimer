#include "cJSON.h"
#include "clock_view.h"
#include "core.h"
#include "display_backend.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hardware.h"
#include "refresh_scheduler.h"
#include "ui.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/time.h>
namespace {
timer_app::Core core("/data");
SemaphoreHandle_t model_mutex;
TaskHandle_t display_task_handle;
bool export_in_progress = false;
timer_app::IdleScreen idle_screen;
bool clock_valid = false, hardware_ok = false, filesystem_ok = false,
     display_ok = true;
std::string system_error;
unsigned up_presses = 0, down_presses = 0;
std::atomic<unsigned> pending_updates{unsigned(display_backend::Update::Full)};
struct Guard {
  Guard() { xSemaphoreTake(model_mutex, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(model_mutex); }
};
void redraw(display_backend::Update requested = display_backend::Update::Full) {
  if (requested == display_backend::Update::None)
    return;
  pending_updates.fetch_or(unsigned(requested));
  if (display_task_handle)
    xTaskNotifyGive(display_task_handle);
}
void wake_timer() {
  if (idle_screen.update(esp_timer_get_time(), true))
    redraw();
}
bool switch_activity(int delta) {
  if (!core.select(delta))
    return false;
  redraw();
  return true;
}
int64_t epoch() { return clock_valid ? (int64_t)time(nullptr) : 0; }
bool send_bytes(const char *b, size_t n) {
  int64_t limit = esp_timer_get_time() + 10000000;
  while (n) {
    int sent = usb_serial_jtag_write_bytes(b, std::min<size_t>(n, 4096),
                                           pdMS_TO_TICKS(100));
    if (sent > 0) {
      b += sent;
      n -= sent;
      limit = esp_timer_get_time() + 10000000;
    } else if (esp_timer_get_time() > limit)
      return false;
  }
  return true;
}
void send_json(cJSON *r) {
  char *s = cJSON_PrintUnformatted(r);
  if (s) {
    send_bytes(s, strlen(s));
    send_bytes("\n", 1);
    cJSON_free(s);
  }
  cJSON_Delete(r);
}
void respond(int id, cJSON *result, const char *error = nullptr) {
  cJSON *r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "id", id);
  cJSON_AddBoolToObject(r, "ok", !error);
  if (error) {
    cJSON_AddStringToObject(r, "error", error);
    cJSON_Delete(result);
  } else
    cJSON_AddItemToObject(r, "result", result ? result : cJSON_CreateObject());
  send_json(r);
}
cJSON *status() {
  if (core.update_daily_stars(epoch(), clock_valid))
    redraw();
  const auto v = core.view(esp_timer_get_time(), clock_valid);
  cJSON *r = cJSON_CreateObject();
  cJSON_AddStringToObject(r, "firmware", "note4c-timer/1.5.9");
  cJSON_AddStringToObject(r, "board", "NOTE4C ESP32-S3 N16R8 SSD2683");
  cJSON_AddBoolToObject(r, "running", v.running);
  cJSON_AddBoolToObject(r, "idle_smile", idle_screen.visible());
  cJSON_AddStringToObject(
      r, "timer_state",
      v.running ? "running" : (v.has_completed_run ? "stopped" : "ready"));
  cJSON_AddNumberToObject(r, "up_presses", up_presses);
  cJSON_AddNumberToObject(r, "down_presses", down_presses);
  cJSON_AddBoolToObject(r, "up_pressed", gpio_get_level(BTN_UP) == 0);
  cJSON_AddBoolToObject(r, "down_pressed", gpio_get_level(BTN_DOWN) == 0);
  cJSON_AddBoolToObject(r, "clock_valid", clock_valid);
  cJSON_AddNumberToObject(r, "utc_offset_minutes", core.utc_offset_minutes());
  const auto local_clock =
      timer_app::format_clock(epoch(), clock_valid, core.utc_offset_minutes());
  cJSON_AddStringToObject(r, "local_time", local_clock.date_time.c_str());
  cJSON_AddStringToObject(r, "timezone", local_clock.zone.c_str());
  cJSON_AddNumberToObject(r, "clock_refresh_seconds", 60);
  cJSON_AddBoolToObject(r, "clock_refresh_when_idle", false);
  cJSON_AddBoolToObject(r, "fast_full_refresh_supported", true);
  cJSON_AddBoolToObject(r, "storage_ok", filesystem_ok && v.storage_ok);
  cJSON_AddBoolToObject(r, "hardware_ok", hardware_ok);
  cJSON_AddBoolToObject(r, "display_ok", display_ok);
  cJSON_AddStringToObject(r, "action", v.name.c_str());
  cJSON_AddNumberToObject(r, "elapsed_ms", v.elapsed_ms);
  if (v.best_ms < 0)
    cJSON_AddNullToObject(r, "best_ms");
  else
    cJSON_AddNumberToObject(r, "best_ms", v.best_ms);
  cJSON_AddNumberToObject(r, "record_count", core.record_count());
  cJSON_AddBoolToObject(r, "stars_available", v.stars_available);
  if (v.stars_available)
    cJSON_AddNumberToObject(r, "daily_stars", v.daily_stars);
  else
    cJSON_AddNullToObject(r, "daily_stars");
  cJSON_AddStringToObject(r, "stars_reset_local_time", "23:59");
  cJSON_AddNumberToObject(r, "running_snapshot_seconds",
                          display_backend::running_snapshot_seconds());
  cJSON_AddBoolToObject(r, "partial_refresh_supported",
                        display_backend::capabilities().number_region_partial);
  cJSON_AddBoolToObject(r, "clock_partial_refresh_supported",
                        display_backend::capabilities().clock_region_partial);
  cJSON *a = cJSON_AddArrayToObject(r, "errors");
  if (!core.error().empty())
    cJSON_AddItemToArray(a, cJSON_CreateString(core.error().c_str()));
  if (!core.star_error().empty())
    cJSON_AddItemToArray(a, cJSON_CreateString(core.star_error().c_str()));
  if (!system_error.empty())
    cJSON_AddItemToArray(a, cJSON_CreateString(system_error.c_str()));
  return r;
}
bool blank_storage() {
  const auto *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
  if (!p)
    return false;
  uint8_t chunk[1024];
  for (size_t off = 0; off < p->size; off += sizeof chunk) {
    if (esp_partition_read(p, off, chunk, sizeof chunk) != ESP_OK)
      return false;
    for (uint8_t c : chunk)
      if (c != 0xff)
        return false;
  }
  return true;
}
void display_task(void *) {
  auto *frame = (uint8_t *)heap_caps_malloc(
      EPD_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!frame) {
    {
      Guard g;
      display_ok = false;
      system_error = "No display buffer";
      display_task_handle = nullptr;
    }
    vTaskDelete(nullptr);
    return;
  }
  bool displayed_running = false, displayed_idle_smile = false;
  size_t displayed_activity = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const unsigned updates = pending_updates.exchange(0);
    if (updates == 0)
      continue;
    timer_app::View v;
    {
      Guard g;
      v = core.view(esp_timer_get_time(), clock_valid);
      v.utc_epoch = epoch();
      v.idle_smile = idle_screen.visible();
      if (!system_error.empty())
        v.error = system_error;
      if (export_in_progress)
        v.error = "USB EXPORT - WAIT TO START";
    }
    render_ui(frame, v);
    using display_backend::Waveform;
    const bool interactive = v.running || displayed_running ||
                             v.index != displayed_activity ||
                             v.idle_smile != displayed_idle_smile;
    const auto waveform =
        interactive ? Waveform::FastPreferred : Waveform::Automatic;
    if (displayed_idle_smile && !v.idle_smile) {
      // Give every pixel a transition into the timer frame, including the
      // background pixels outside the smile.
      for (size_t i = 0; i < EPD_FRAME_BYTES; ++i)
        frame[i] ^= 0x55;
      display_backend::show(frame, display_backend::Update::Full, waveform);
      for (size_t i = 0; i < EPD_FRAME_BYTES; ++i)
        frame[i] ^= 0x55;
    }
    const bool ok = display_backend::show(
        frame, static_cast<display_backend::Update>(updates), waveform);
    if (ok) {
      displayed_running = v.running;
      displayed_idle_smile = v.idle_smile;
      displayed_activity = v.index;
    }
    {
      Guard g;
      display_ok = ok;
      if (!ok)
        system_error = "Display failed; check USB status / reset";
    }
  }
}
void history(int id) {
  FILE *f = fopen(core.history_path().c_str(), "rb");
  if (!f) {
    respond(id, nullptr, "Cannot open history.json");
    return;
  }
  char prefix[80];
  snprintf(prefix, sizeof prefix, "{\"id\":%d,\"ok\":true,\"result\":", id);
  bool ok = send_bytes(prefix, strlen(prefix));
  char buf[2048];
  size_t n;
  while (ok && (n = fread(buf, 1, sizeof buf,
                          f))) { // Remove physical newlines so transport
                                 // remains one JSON line.
    size_t w = 0;
    for (size_t i = 0; i < n; i++)
      if (buf[i] != '\n' && buf[i] != '\r')
        buf[w++] = buf[i];
    ok = send_bytes(buf, w);
  }
  bool read_ok = !ferror(f);
  fclose(f);
  if (ok && read_ok)
    send_bytes("}\n", 2); // No success terminator after failed read; host times
                          // out instead of accepting a partial export.
}
void command(const std::string &line) {
  cJSON *r = cJSON_ParseWithLength(line.c_str(), line.size() + 1);
  const auto *rid = cJSON_GetObjectItemCaseSensitive(r, "id");
  const auto *cmd = cJSON_GetObjectItemCaseSensitive(r, "cmd");
  if (!cJSON_IsNumber(rid) || !std::isfinite(rid->valuedouble) ||
      rid->valuedouble < 0 || rid->valuedouble > 2147483647 ||
      floor(rid->valuedouble) != rid->valuedouble || !cJSON_IsString(cmd)) {
    cJSON_Delete(r);
    return;
  }
  int id = rid->valueint;
  std::string name = cmd->valuestring;
  cJSON *result = nullptr;
  std::string error;
  bool exporting = false;
  {
    Guard guard;
    if (name == "status")
      result = status();
    else if (name == "previous_activity" || name == "next_activity") {
      wake_timer();
      if (switch_activity(name == "previous_activity" ? -1 : 1))
        result = status();
      else
        error = core.error();
    } else if (name == "get_actions")
      result = core.actions_json();
    else if (name == "clear_history") {
      const auto *confirm = cJSON_GetObjectItemCaseSensitive(r, "confirm");
      if (!cJSON_IsString(confirm) || strcmp(confirm->valuestring, "CLEAR") != 0)
        error = "Clear history requires explicit confirmation";
      else if (core.clear_history()) {
        result = status();
        redraw();
      } else
        error = core.error();
    } else if (name == "get_history") {
      if (core.running())
        error = "Stop timer before exporting history";
      else {
        wake_timer();
        exporting = true;
        export_in_progress = true;
        redraw();
      }
    } else if (name == "set_actions") {
      wake_timer();
      if (core.set_actions(cJSON_GetObjectItemCaseSensitive(r, "actions")))
        result = core.actions_json();
      else
        error = core.error();
      redraw();
    } else if (name == "set_utc_offset") {
      const auto *value =
          cJSON_GetObjectItemCaseSensitive(r, "utc_offset_minutes");
      if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
          floor(value->valuedouble) != value->valuedouble ||
          value->valuedouble < -720 || value->valuedouble > 840)
        error = "UTC offset must be integer -720 through +840 minutes";
      else if (!core.set_utc_offset_minutes((int)value->valuedouble))
        error = core.error();
      else
        result = status();
      redraw();
    } else if (name == "set_time") {
      const auto *value = cJSON_GetObjectItemCaseSensitive(r, "epoch");
      const auto *offset =
          cJSON_GetObjectItemCaseSensitive(r, "utc_offset_minutes");
      const bool valid = cJSON_IsNumber(value) &&
                         std::isfinite(value->valuedouble) &&
                         value->valuedouble >= 1704067200LL &&
                         value->valuedouble < 4102444800LL &&
                         floor(value->valuedouble) == value->valuedouble;
      const bool offset_valid =
          !offset ||
          (cJSON_IsNumber(offset) && std::isfinite(offset->valuedouble) &&
           floor(offset->valuedouble) == offset->valuedouble &&
           offset->valuedouble >= -720 && offset->valuedouble <= 840);
      if (core.running())
        error = "Stop timer before changing clock";
      else if (!valid)
        error = "epoch must be an integer UTC time from 2024 through 2099";
      else if (!offset_valid)
        error = "UTC offset must be integer -720 through +840 minutes";
      else if (!filesystem_ok || !core.healthy())
        error = "Storage unavailable; cannot establish UTC clock";
      else if (offset && !core.set_utc_offset_minutes((int)offset->valuedouble))
        error = core.error();
      else if (!rtc_set_epoch((int64_t)value->valuedouble)) {
        clock_valid = false;
        error = "RTC write/readback failed; clock not accepted";
      } else if (!core.save_utc_marker()) {
        clock_valid = false;
        error = core.error();
      } else {
        timeval tv{(time_t)value->valuedouble, 0};
        settimeofday(&tv, nullptr);
        clock_valid = true;
        result = status();
      }
      redraw();
    } else if (name == "start" || name == "stop") {
      wake_timer();
      bool ok = name == "start" ? core.start(esp_timer_get_time(), epoch())
                                : core.stop(esp_timer_get_time(), epoch());
      if (ok)
        result = status();
      else
        error = core.error();
      redraw();
    } else
      error = "Unknown command";
  }
  // Never hold the model/input mutex during potentially stalled USB output.
  if (exporting) {
    history(id);
    {
      Guard guard;
      export_in_progress = false;
    }
    redraw();
  } else
    respond(id, result, error.empty() ? nullptr : error.c_str());
  cJSON_Delete(r);
}
void usb_task(void *) {
  std::string line;
  line.reserve(8192);
  uint8_t b[256];
  bool overflow = false;
  for (;;) {
    int n = usb_serial_jtag_read_bytes(b, sizeof b, pdMS_TO_TICKS(100));
    for (int i = 0; i < n; i++) {
      if (b[i] == '\n') {
        if (!overflow && !line.empty())
          command(line);
        line.clear();
        overflow = false;
      } else if (b[i] != '\r') {
        if (line.size() < 8192)
          line.push_back((char)b[i]);
        else
          overflow = true;
      }
    }
  }
}
struct Button {
  gpio_num_t pin;
  int raw = 1, stable = 1;
  int64_t changed = 0;
  bool pressed(int64_t now) {
    int r = gpio_get_level(pin);
    if (r != raw) {
      raw = r;
      changed = now;
    }
    if (raw != stable && now - changed >= 20000) {
      stable = raw;
      return stable == 0;
    }
    return false;
  }
};
} // namespace
extern "C" void app_main() {
  hardware_ok = hardware_init();
  model_mutex = xSemaphoreCreateMutex();
  if (!model_mutex)
    return;
  setenv("TZ", "UTC0", 1);
  tzset();
  esp_vfs_littlefs_conf_t fs{};
  fs.base_path = "/data";
  fs.partition_label = "storage";
  fs.format_if_mount_failed = blank_storage();
  fs.dont_mount = false;
  filesystem_ok = esp_vfs_littlefs_register(&fs) == ESP_OK;
  if (filesystem_ok) {
    if (!core.init())
      system_error = "Storage recovery needs attention";
  } else
    system_error = "Storage unreadable; preserved. Export backup.";
  // A plausible old RTC calendar may be local time from previous firmware.
  // Trust UTC only after this timer has persisted a successful USB time sync.
  int64_t rtc_epoch = 0;
  if (filesystem_ok && core.healthy() && core.has_utc_marker() &&
      rtc_get_epoch(rtc_epoch) && rtc_epoch >= 1704067200LL) {
    timeval tv{(time_t)rtc_epoch, 0};
    settimeofday(&tv, nullptr);
    clock_valid = true;
  }
  core.update_daily_stars(epoch(), clock_valid);
  if (!hardware_ok)
    system_error = "Hardware init failed; check USB status";
  usb_serial_jtag_driver_config_t usb{};
  usb.rx_buffer_size = 1024;
  usb.tx_buffer_size = 4096;
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
  idle_screen = timer_app::IdleScreen(esp_timer_get_time());
  xTaskCreate(display_task, "epd", 6144, nullptr, 2, &display_task_handle);
  xTaskCreate(usb_task, "usb", 8192, nullptr, 3, nullptr);
  redraw();
  Button buttons[] = {{BTN_UP}, {BTN_DOWN}, {BTN_OK}};
  // Avoid treating the power-on / bootloader button still held at boot as a
  // tap.
  for (auto &b : buttons) {
    b.raw = b.stable = gpio_get_level(b.pin);
    b.changed = esp_timer_get_time();
  }
  timer_app::RefreshScheduler refresh_scheduler;
  for (;;) {
    int64_t now = esp_timer_get_time();
    {
      Guard guard;
      // Reset stars independently of display completion, even while idle.
      if (core.update_daily_stars(epoch(), clock_valid))
        redraw(); // Stars lie outside the clock/number partial regions.
    }
    now = esp_timer_get_time(); // Idle reset writes must not stale input time.
    const bool up = buttons[0].pressed(now);
    const bool down = buttons[1].pressed(now);
    const bool ok = buttons[2].pressed(now);
    if (up || down || ok) {
      Guard guard;
      wake_timer();
      up_presses += up;
      down_presses += down;
      if (ok && !export_in_progress) {
        if (core.running())
          core.stop(now, epoch());
        else
          core.start(now, epoch());
        redraw();
      } else if (!core.running() && up != down) {
        switch_activity(up ? -1 : 1);
      }
    }
    {
      Guard guard;
      if (idle_screen.update(esp_timer_get_time(),
                             core.running() || export_in_progress))
        redraw();
      led_set(core.running() && ((now / 500000) % 2 == 0));
      redraw(refresh_scheduler.poll(
          now, epoch(), clock_valid, core.running(),
          display_backend::capabilities().number_region_partial));
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
