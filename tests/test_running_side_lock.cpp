#include "core.h"
#include "display_backend.h"
#include "cJSON.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
using timer_app::Core;
using display_backend::Update;
constexpr int64_t kEpoch = 1793707200; // 2026-11-03 12:00:00 UTC.
int checks = 0;
#define CHECK(condition)                                                        \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "Running side-lock failed at line " << __LINE__ << ": "     \
                << #condition << '\n';                                         \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

struct TempDir {
  std::string path;
  TempDir() {
    char pattern[] = "/tmp/note4c-side-lock-XXXXXX";
    const char *created = mkdtemp(pattern);
    CHECK(created != nullptr);
    path = created;
  }
  ~TempDir() { std::filesystem::remove_all(path); }
};
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
std::string read(const std::string &path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}
Json parse(const std::string &contents) {
  Json doc(cJSON_Parse(contents.c_str()), cJSON_Delete);
  CHECK(doc != nullptr);
  return doc;
}
std::string string_field(const cJSON *doc, const char *key) {
  const auto *value = cJSON_GetObjectItemCaseSensitive(doc, key);
  CHECK(cJSON_IsString(value));
  return value->valuestring;
}

// Only the mock panel runs on another thread. Every Core operation stays on
// the control/test thread, just as application controls are serialized.
struct PanelBarrier {
  std::mutex mutex;
  std::condition_variable changed;
  size_t entered = 0, released = 0, completed = 0;
  bool timed_out = false, unexpected_call = false;
  std::vector<bool> answers;

  explicit PanelBarrier(std::vector<bool> results) : answers(std::move(results)) {}
  bool show() {
    std::unique_lock<std::mutex> lock(mutex);
    const size_t call = ++entered;
    changed.notify_all();
    if (!changed.wait_for(lock, std::chrono::seconds(5),
                          [&] { return released >= call; }))
      timed_out = true;
    ++completed;
    changed.notify_all();
    if (call > answers.size()) {
      unexpected_call = true;
      return false;
    }
    return !timed_out && answers[call - 1];
  }
  void wait_for_call(size_t call) {
    std::unique_lock<std::mutex> lock(mutex);
    CHECK(changed.wait_for(lock, std::chrono::seconds(5),
                          [&] { return entered >= call; }));
  }
  void release(size_t call) {
    std::lock_guard<std::mutex> lock(mutex);
    released = call;
    changed.notify_all();
  }
  void check_in_flight(size_t call) {
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(entered == call);
    CHECK(completed == call - 1);
    CHECK(!timed_out);
  }
  void check_finished(size_t calls) {
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(entered == calls);
    CHECK(completed == calls);
    CHECK(!timed_out);
    CHECK(!unexpected_call);
  }
};
PanelBarrier *active_panel = nullptr;

void initialize(Core &core) {
  CHECK(core.init());
  CHECK(core.set_utc_offset_minutes(0));
  core.update_daily_stars(kEpoch, true);
  CHECK(core.healthy());
}
void check_sides_blocked(Core &core, const timer_app::View &selected) {
  for (int attempt = 0; attempt < 10; ++attempt) {
    CHECK(!core.select(-1));
    CHECK(!core.select(1));
    const auto view = core.view(2000000, true);
    CHECK(view.index == selected.index);
    CHECK(view.name == selected.name);
  }
}
void check_saved_run(const TempDir &dir, const std::string &id,
                     const std::string &name, int64_t duration_ms) {
  const auto history = parse(read(dir.path + "/history.json"));
  const auto *records = cJSON_GetObjectItemCaseSensitive(history.get(), "records");
  CHECK(cJSON_GetArraySize(records) == 1);
  const auto *record = cJSON_GetArrayItem(records, 0);
  CHECK(string_field(record, "action_id") == id);
  CHECK(string_field(record, "action_name") == name);
  CHECK(string_field(record, "status") == "completed");
  const auto *duration = cJSON_GetObjectItemCaseSensitive(record, "duration_ms");
  CHECK(cJSON_IsNumber(duration));
  CHECK(duration->valuedouble == duration_ms);
}

void idle_display_side_lock() {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  CHECK(core.select(1));
  const auto selected = core.view(0, true);
  CHECK(core.start(1000000, kEpoch));
  const auto pending_before = read(dir.path + "/pending.json");
  check_sides_blocked(core, selected);
  CHECK(core.running());
  CHECK(read(dir.path + "/pending.json") == pending_before);
  CHECK(core.stop(5321000, kEpoch + 4));
  CHECK(!core.running());
  CHECK(core.view(900000000, true).elapsed_ms == 4321);
  const auto pending = parse(pending_before);
  check_saved_run(dir, string_field(pending.get(), "action_id"), selected.name, 4321);
  CHECK(core.select(-1));
  CHECK(core.view(0, true).index != selected.index);
  CHECK(core.select(1));
  CHECK(core.view(0, true).index == selected.index);
}

void panel_completion_does_not_unlock_running(bool panel_success) {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  const auto selected = core.view(0, true);
  CHECK(core.start(1000000, kEpoch));
  PanelBarrier panel({panel_success});
  active_panel = &panel;
  std::array<uint8_t, 30000> frame{};
  bool display_result = !panel_success;
  std::thread worker([&] { display_result = display_backend::show(frame.data(), Update::Full); });
  panel.wait_for_call(1);
  check_sides_blocked(core, selected);
  panel.check_in_flight(1);
  panel.release(1);
  worker.join();
  active_panel = nullptr;
  CHECK(display_result == panel_success);
  panel.check_finished(1);
  CHECK(core.running());
  check_sides_blocked(core, selected); // Success or failure cannot unlock sides.
  CHECK(core.stop(5321000, kEpoch + 4));
  CHECK(core.view(0, true).elapsed_ms == 4321);
  CHECK(core.select(1));
  CHECK(core.view(0, true).index != selected.index);
}

void front_stop_works_with_panel_in_flight(bool panel_success) {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  CHECK(core.select(1));
  const auto selected = core.view(0, true);
  CHECK(core.start(1000000, kEpoch));
  const auto pending_before = read(dir.path + "/pending.json");
  PanelBarrier panel({panel_success});
  active_panel = &panel;
  std::array<uint8_t, 30000> frame{};
  bool display_result = !panel_success;
  std::thread worker([&] {
    display_result = display_backend::show(frame.data(), Update::ClockAndNumber);
  });
  panel.wait_for_call(1);
  check_sides_blocked(core, selected);
  CHECK(read(dir.path + "/pending.json") == pending_before);
  panel.check_in_flight(1);
  CHECK(core.stop(12345000, kEpoch + 11)); // Front-stop model action.
  panel.check_in_flight(1); // Stop finished before the mock panel was released.
  CHECK(!core.running());
  CHECK(core.view(900000000, true).elapsed_ms == 11345);
  CHECK(core.view(0, true).index == selected.index);
  CHECK(core.view(0, true).name == selected.name);
  const auto pending = parse(pending_before);
  check_saved_run(dir, string_field(pending.get(), "action_id"), selected.name, 11345);
  CHECK(core.select(-1)); // Stopped side selection works even while panel is busy.
  CHECK(core.view(0, true).index != selected.index);
  panel.check_in_flight(1);
  panel.release(1);
  worker.join();
  active_panel = nullptr;
  CHECK(display_result == panel_success);
  panel.check_finished(1);
  CHECK(!core.running());
  CHECK(core.select(1));
  CHECK(core.view(0, true).name == selected.name);
  CHECK(core.view(0, true).elapsed_ms == 0);
  CHECK(core.view(0, true).best_ms == 11345);
  CHECK(!core.view(0, true).has_completed_run);
}

void successive_pending_refreshes_do_not_gate_controls(bool first_success) {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  const auto selected = core.view(0, true);
  CHECK(core.start(1000000, kEpoch));
  PanelBarrier panel({first_success, !first_success});
  active_panel = &panel;
  std::array<uint8_t, 30000> frame{};
  std::array<bool, 2> results{};
  // Two serialized backend calls model a refresh followed by a pending refresh.
  // This deliberately does not claim to exercise FreeRTOS's notification queue.
  std::thread worker([&] {
    results[0] = display_backend::show(frame.data(), Update::Full);
    results[1] = display_backend::show(frame.data(), Update::ClockAndNumber);
  });
  panel.wait_for_call(1);
  check_sides_blocked(core, selected);
  panel.release(1);
  panel.wait_for_call(2);
  CHECK(core.running());
  check_sides_blocked(core, selected);
  panel.check_in_flight(2);
  CHECK(core.stop(8765000, kEpoch + 7));
  panel.check_in_flight(2);
  CHECK(core.view(0, true).elapsed_ms == 7765);
  CHECK(core.select(1));
  panel.release(2);
  worker.join();
  active_panel = nullptr;
  CHECK(results[0] == first_success);
  CHECK(results[1] == !first_success);
  panel.check_finished(2);
  CHECK(core.select(-1));
  CHECK(core.view(0, true).name == selected.name);
  CHECK(core.view(0, true).elapsed_ms == 0);
  CHECK(core.view(0, true).best_ms == 7765);
  CHECK(!core.view(0, true).has_completed_run);
}

void side_lock_survives_pending_save() {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  CHECK(core.select(1));
  const auto selected = core.view(0, true);
  CHECK(core.start(1000000, kEpoch));
  const auto pending = parse(read(dir.path + "/pending.json"));
  CHECK(std::filesystem::create_directory(dir.path + "/pending.json.tmp"));
  CHECK(!core.stop(5321000, kEpoch + 4));
  CHECK(!core.running());
  CHECK(core.view(900000000, true).elapsed_ms == 4321);
  check_sides_blocked(core, selected); // Failed saves retain the activity lock.
  CHECK(core.record_count() == 0);
  CHECK(std::filesystem::remove(dir.path + "/pending.json.tmp"));
  CHECK(core.stop(900000000, kEpoch + 900)); // Retry retains the original stop.
  CHECK(core.healthy());
  CHECK(core.view(0, true).elapsed_ms == 4321);
  check_saved_run(dir, string_field(pending.get(), "action_id"), selected.name, 4321);
  CHECK(core.select(-1));
  CHECK(core.view(0, true).index != selected.index);
}
} // namespace

// The production backend calls epd_show; this host test substitutes only that
// hardware boundary, not Core or the backend's full-refresh fallback behavior.
bool epd_show(const uint8_t *, display_backend::Waveform) {
  if (!active_panel)
    std::abort();
  return active_panel->show();
}

int main() {
  idle_display_side_lock();
  for (bool result : {true, false}) {
    panel_completion_does_not_unlock_running(result);
    front_stop_works_with_panel_in_flight(result);
    successive_pending_refreshes_do_not_gate_controls(result);
  }
  side_lock_survives_pending_save();
  std::cout << checks << " running-side-lock assertions passed\n";
}
