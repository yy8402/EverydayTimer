#include "core.h"
#include "cJSON.h"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

using timer_app::Core;
namespace {
int checks = 0;
constexpr int64_t kDay = 1793664000; // 2026-11-03 00:00:00 UTC.
constexpr int64_t kReset = kDay + 23 * 3600 + 59 * 60;
#define CHECK(condition)                                                        \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "Daily stars failed at line " << __LINE__ << ": "           \
                << #condition << '\n';                                         \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

struct TempDir {
  std::string path;
  TempDir() {
    char pattern[] = "/tmp/note4c-stars-XXXXXX";
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
void write(const std::string &path, const std::string &contents) {
  std::ofstream output(path);
  output << contents;
  CHECK(output.good());
}
Json parse(const std::string &contents) {
  Json result(cJSON_Parse(contents.c_str()), cJSON_Delete);
  CHECK(result != nullptr);
  return result;
}
std::string encode(const cJSON *doc) {
  char *raw = cJSON_PrintUnformatted(doc);
  CHECK(raw != nullptr);
  std::string result(raw);
  cJSON_free(raw);
  return result;
}
int64_t number(const cJSON *doc, const char *key) {
  const auto *value = cJSON_GetObjectItemCaseSensitive(doc, key);
  CHECK(cJSON_IsNumber(value));
  return static_cast<int64_t>(value->valuedouble);
}
int64_t generation(const TempDir &dir) {
  const auto state = parse(read(dir.path + "/stars.json"));
  return number(state.get(), "generation");
}
void initialize(Core &core, int64_t epoch = kDay + 12 * 3600) {
  CHECK(core.init());
  CHECK(!core.view(0, true).stars_available);
  CHECK(core.set_utc_offset_minutes(0));
  CHECK(core.update_daily_stars(epoch, true));
  CHECK(core.view(0, true).stars_available);
}
void run(Core &core, int64_t duration_ms, int64_t epoch = kDay + 12 * 3600) {
  CHECK(core.start(1000000, epoch));
  CHECK(core.stop(1000000 + duration_ms * 1000,
                  epoch + (duration_ms + 999) / 1000));
}
void stars(const Core &core, uint64_t expected) {
  const auto view = core.view(0, true);
  CHECK(view.stars_available);
  CHECK(view.daily_stars == expected);
}
void set_actions(Core &core, const char *json) {
  const auto doc = parse(json);
  CHECK(core.set_actions(doc.get()));
}
std::string completed_record(const TempDir &dir, int index = 0) {
  const auto history = parse(read(dir.path + "/history.json"));
  const auto *records = cJSON_GetObjectItemCaseSensitive(history.get(), "records");
  const auto *record = cJSON_GetArrayItem(records, index);
  CHECK(record != nullptr);
  return encode(record);
}

void awards_and_activity_changes() {
  TempDir dir;
  {
    Core core(dir.path);
    initialize(core);
    stars(core, 0);
    run(core, 3000);
    stars(core, 1); // The first completed benchmark counts.
    run(core, 3000);
    stars(core, 1); // Equal milliseconds are a tie.
    run(core, 4000);
    stars(core, 1);
    run(core, 2000);
    stars(core, 2);
    CHECK(core.select(1));
    run(core, 5000);
    stars(core, 3); // Awards are cumulative across activities.
    set_actions(core, "{\"schema_version\":1,\"actions\":["
                      "{\"id\":\"activity-1\",\"name\":\"Renamed\"}]}");
    stars(core, 3); // Removing the other activity does not remove its award.
    CHECK(core.view(0, true).best_ms == 2000);
    set_actions(core, "{\"schema_version\":1,\"actions\":["
                      "{\"id\":\"activity-2\",\"name\":\"Restored\"}]}");
    stars(core, 3);
    CHECK(core.view(0, true).best_ms == 5000);
    CHECK(core.record_count() == 5);
  }
  for (int restart = 0; restart < 3; ++restart) {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(!core.view(0, true).stars_available);
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 3);
    CHECK(core.record_count() == 5);
    CHECK(core.view(0, true).best_ms == 5000);
    CHECK(!core.update_daily_stars(kDay + 13 * 3600 + 1, true));
  }
}

void reset_boundaries_and_power_off() {
  TempDir dir;
  std::string history;
  {
    Core core(dir.path);
    initialize(core);
    run(core, 3000);
    history = read(dir.path + "/history.json");
    CHECK(!core.update_daily_stars(kReset - 1, true));
    stars(core, 1);
    const auto old_generation = generation(dir);
    CHECK(core.update_daily_stars(kReset, true));
    stars(core, 0);
    CHECK(generation(dir) > old_generation);
    CHECK(core.view(0, true).best_ms == 3000);
    CHECK(read(dir.path + "/history.json") == history);
    CHECK(!core.update_daily_stars(kReset + 1, true));
    run(core, 2000, kReset + 10); // A win during 23:59 belongs to the new interval.
    stars(core, 1);
    CHECK(!core.update_daily_stars(kDay + 86400, true));
    stars(core, 1); // Midnight is not a second reset.
  }
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 86400 + 1, true));
    stars(core, 1);
    CHECK(core.view(0, true).best_ms == 2000);
  }
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kReset + 3 * 86400, true));
    stars(core, 0); // Several powered-off reset boundaries are reconciled once.
    CHECK(core.view(0, true).best_ms == 2000);
    CHECK(core.record_count() == 2);
  }

  TempDir crossing;
  Core core(crossing.path);
  initialize(core);
  run(core, 4000);
  CHECK(core.start(1000000, kReset - 2));
  CHECK(core.stop(4000000, kReset + 1));
  stars(core, 1); // Stop time, not start time, determines the award interval.
  CHECK(core.view(0, true).best_ms == 3000);
}

void clock_changes_and_offsets() {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  run(core, 5000);
  const auto first_generation = generation(dir);
  CHECK(!core.update_daily_stars(kDay + 1, true));
  stars(core, 1); // A backwards adjustment within an interval preserves stars.
  CHECK(generation(dir) == first_generation);
  CHECK(core.update_daily_stars(kDay - 3600, true));
  stars(core, 0);
  const auto backward_generation = generation(dir);
  CHECK(backward_generation > first_generation);
  run(core, 4000, kDay - 3600);
  stars(core, 1);
  CHECK(core.update_daily_stars(kDay + 12 * 3600, true));
  stars(core, 0); // Revisiting a previous date never resurrects its awards.
  CHECK(generation(dir) > backward_generation);
  CHECK(core.update_daily_stars(kDay + 100 * 86400, true) == false);
  CHECK(core.update_daily_stars(kDay + 12 * 3600, true) == false);
  run(core, 3000);
  stars(core, 1); // Correcting a future clock does not lock out new awards.
  const auto persisted = read(dir.path + "/stars.json");
  CHECK(core.update_daily_stars(0, false));
  CHECK(!core.view(0, false).stars_available);
  CHECK(!core.update_daily_stars(0, true));
  CHECK(!core.update_daily_stars(4102444800LL, true));
  CHECK(read(dir.path + "/stars.json") == persisted);
  CHECK(core.update_daily_stars(kDay + 12 * 3600, true));
  stars(core, 1);
  CHECK(core.set_utc_offset_minutes(60));
  CHECK(!core.update_daily_stars(kDay + 12 * 3600, true));
  stars(core, 1);
  CHECK(core.set_utc_offset_minutes(840));
  CHECK(core.update_daily_stars(kDay + 12 * 3600, true));
  stars(core, 0);
  CHECK(core.set_utc_offset_minutes(0));
  CHECK(!core.update_daily_stars(kDay + 12 * 3600, true));
  stars(core, 0);

  for (const int offset : {-720, -210, 345, 480, 840}) {
    TempDir timezone_dir;
    Core timezone_core(timezone_dir.path);
    CHECK(timezone_core.init());
    CHECK(timezone_core.set_utc_offset_minutes(offset));
    const int64_t reset_utc = kReset - int64_t(offset) * 60;
    CHECK(timezone_core.update_daily_stars(reset_utc - 20, true));
    run(timezone_core, 1000, reset_utc - 10);
    stars(timezone_core, 1);
    CHECK(!timezone_core.update_daily_stars(reset_utc - 1, true));
    CHECK(timezone_core.update_daily_stars(reset_utc, true));
    stars(timezone_core, 0);
  }
}

void interrupted_and_completed_recovery() {
  TempDir interrupted;
  {
    Core core(interrupted.path);
    initialize(core);
    run(core, 5000);
    CHECK(core.start(1000000, kDay + 13 * 3600));
  }
  {
    Core core(interrupted.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 1);
    CHECK(core.record_count() == 2);
    CHECK(core.view(0, true).best_ms == 5000);
  }

  TempDir recovered;
  std::string empty, record, committed;
  {
    Core core(recovered.path);
    initialize(core);
    empty = read(recovered.path + "/history.json");
    run(core, 1234);
    record = completed_record(recovered);
    committed = read(recovered.path + "/history.json");
  }
  // Check every torn-append position with the completed award still journaled.
  for (size_t cut = 0; cut <= record.size() + 1; ++cut) {
    write(recovered.path + "/history.json",
          empty.substr(0, empty.size() - 3) + (record + "\n").substr(0, cut));
    write(recovered.path + "/pending.json", record + "\n");
    Core core(recovered.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 1);
    CHECK(core.record_count() == 1);
    CHECK(core.view(0, true).best_ms == 1234);
    CHECK(!std::filesystem::exists(recovered.path + "/pending.json"));
    CHECK(read(recovered.path + "/history.json") == committed);
  }
  write(recovered.path + "/pending.json", record + "\n");
  {
    Core core(recovered.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 1);
    CHECK(core.record_count() == 1);
  }
  write(recovered.path + "/history.json", empty);
  write(recovered.path + "/pending.json", record + "\n");
  {
    Core core(recovered.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kReset + 1, true));
    stars(core, 0); // Recovery after reset preserves the run, not yesterday's star.
    CHECK(core.record_count() == 1);
    CHECK(core.view(0, true).best_ms == 1234);
  }
}

void missing_stale_and_damaged_state() {
  TempDir dir;
  int64_t original_generation;
  {
    Core core(dir.path);
    initialize(core);
    run(core, 5000);
    original_generation = generation(dir);
  }
  std::filesystem::remove(dir.path + "/stars.json");
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 0);
    CHECK(generation(dir) > original_generation);
    run(core, 4000);
    stars(core, 1);
  }
  const auto newer_generation = generation(dir);
  auto state = parse(read(dir.path + "/stars.json"));
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(state.get(), "generation"),
                      original_generation);
  write(dir.path + "/stars.json", encode(state.get()));
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 0);
    CHECK(generation(dir) > newer_generation);
    CHECK(core.view(0, true).best_ms == 4000);
  }
  for (const std::string damaged : {"not JSON", "{}",
                                    "{\"schema_version\":1,\"generation\":-1,\"bucket\":1}"}) {
    write(dir.path + "/stars.json", damaged);
    Core core(dir.path);
    CHECK(core.init());
    CHECK(!core.update_daily_stars(kDay + 13 * 3600, true));
    CHECK(!core.view(0, true).stars_available);
    CHECK(core.healthy());
    run(core, 3000);
    CHECK(core.view(0, true).best_ms == 3000);
    CHECK(read(dir.path + "/stars.json") == damaged);
  }
}

void optional_metadata_and_generation_recovery() {
  TempDir seed;
  std::string record, state, empty;
  int64_t original_generation;
  {
    Core core(seed.path);
    initialize(core);
    empty = read(seed.path + "/history.json");
    run(core, 5000);
    record = completed_record(seed);
    state = read(seed.path + "/stars.json");
    original_generation = generation(seed);
    const auto award = parse(record);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(award.get(), "new_best")));
    CHECK(number(award.get(), "star_generation") == original_generation);
    CHECK(number(award.get(), "star_bucket") == (kDay + 12 * 3600 + 60) / 86400);
  }
  // Invalid or absent optional metadata must never invalidate the normal run.
  for (int variant = 0; variant < 7; ++variant) {
    TempDir dir;
    { Core core(dir.path); CHECK(core.init()); CHECK(core.set_utc_offset_minutes(0)); }
    write(dir.path + "/stars.json", state);
    const auto changed = parse(record);
    if (variant == 0)
      cJSON_ReplaceItemInObject(changed.get(), "star_generation", cJSON_CreateNumber(-1));
    else if (variant == 1)
      cJSON_ReplaceItemInObject(changed.get(), "star_generation", cJSON_CreateString("1"));
    else if (variant == 2)
      cJSON_ReplaceItemInObject(changed.get(), "star_bucket", cJSON_CreateNumber(1.5));
    else if (variant == 3)
      cJSON_DeleteItemFromObject(changed.get(), "star_bucket");
    else if (variant == 4)
      cJSON_ReplaceItemInObject(changed.get(), "new_best", cJSON_CreateString("true"));
    else if (variant == 5)
      cJSON_ReplaceItemInObject(changed.get(), "star_bucket", cJSON_CreateNumber(1));
    else {
      cJSON_DeleteItemFromObject(changed.get(), "new_best");
      cJSON_DeleteItemFromObject(changed.get(), "star_generation");
      cJSON_DeleteItemFromObject(changed.get(), "star_bucket");
    }
    const std::string history = empty.substr(0, empty.size() - 3) +
                                encode(changed.get()) + "\n]}\n";
    write(dir.path + "/history.json", history);
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 0);
    CHECK(core.record_count() == 1);
    CHECK(core.view(0, true).best_ms == 5000);
    CHECK(read(dir.path + "/history.json") == history);
    run(core, 4000);
    stars(core, 1);
  }

  // The flag alone is not enough: replay still verifies a genuinely lower best.
  {
    TempDir dir;
    { Core core(dir.path); CHECK(core.init()); CHECK(core.set_utc_offset_minutes(0)); }
    write(dir.path + "/stars.json", state);
    const auto tie = parse(record);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(tie.get(), "seq"), 2);
    write(dir.path + "/history.json", empty.substr(0, empty.size() - 3) +
          record + "\n," + encode(tie.get()) + "\n]}\n");
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 1);
    CHECK(core.record_count() == 2);
  }

  // Missing state must include an as-yet-uncommitted completed award in its max.
  write(seed.path + "/history.json", empty);
  write(seed.path + "/pending.json", record + "\n");
  CHECK(std::filesystem::remove(seed.path + "/stars.json"));
  {
    Core core(seed.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 0);
    CHECK(generation(seed) > original_generation);
    CHECK(core.record_count() == 1);
    CHECK(core.view(0, true).best_ms == 5000);
    run(core, 4000);
    stars(core, 1);
    set_actions(core, "{\"schema_version\":1,\"actions\":["
                      "{\"id\":\"other\",\"name\":\"Other\"}]}");
    stars(core, 1);
    original_generation = generation(seed);
  }
  CHECK(std::filesystem::remove(seed.path + "/stars.json"));
  {
    Core core(seed.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
    stars(core, 0);
    CHECK(generation(seed) > original_generation); // Includes removed IDs.
    CHECK(core.record_count() == 2);
  }
}

void repeated_stop_after_journal_failure() {
  TempDir dir;
  Core core(dir.path);
  initialize(core);
  run(core, 5000);
  CHECK(core.start(1000000, kReset - 10));
  CHECK(std::filesystem::create_directory(dir.path + "/pending.json.tmp"));
  CHECK(!core.stop(4000000, kReset - 7));
  CHECK(!core.view(0, true).stars_available);
  CHECK(core.view(0, true).daily_stars == 0);
  CHECK(core.record_count() == 1);
  CHECK(std::filesystem::remove(dir.path + "/pending.json.tmp"));
  CHECK(core.stop(9000000, kReset + 20)); // Must retain the original frozen stop.
  CHECK(core.record_count() == 2);
  CHECK(core.view(0, true).best_ms == 3000);
  CHECK(core.update_daily_stars(kReset + 20, true));
  stars(core, 0); // Retrying after the boundary cannot move yesterday's award.
  CHECK(!core.stop(10000000, kReset + 21));
  CHECK(core.record_count() == 2);
  stars(core, 0);
}

void metadata_free_v12_history() {
  TempDir dir;
  {
    Core core(dir.path);
    initialize(core);
  }
  const std::string legacy =
      "{\"schema_version\":1,\"records\":[\n"
      "{\"seq\":1,\"action_id\":\"activity-1\",\"action_name\":\"Activity 1\","
      "\"started_at\":\"2026-11-03T12:00:00Z\",\"ended_at\":\"2026-11-03T12:00:05Z\","
      "\"duration_ms\":5000,\"status\":\"completed\"}\n"
      ",{\"seq\":2,\"action_id\":\"activity-1\",\"action_name\":\"Activity 1\","
      "\"started_at\":\"2026-11-03T12:01:00Z\",\"ended_at\":\"2026-11-03T12:01:04Z\","
      "\"duration_ms\":4000,\"status\":\"completed\"}\n]}\n";
  write(dir.path + "/history.json", legacy);
  Core core(dir.path);
  CHECK(core.init());
  CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
  CHECK(core.record_count() == 2);
  CHECK(core.view(0, true).best_ms == 4000);
  stars(core, 0); // Upgrading firmware does not retroactively manufacture stars.
  CHECK(read(dir.path + "/history.json") == legacy);
  run(core, 4000);
  stars(core, 0); // A new tie also does not count.
  run(core, 3000);
  stars(core, 1);
  CHECK(core.record_count() == 4);
  CHECK(core.view(0, true).best_ms == 3000);
}

void historical_id_cap_is_cosmetic() {
  CHECK(timer_app::kMaxStarActionIDs == 1024);
  for (const size_t unique_ids : {timer_app::kMaxStarActionIDs,
                                  timer_app::kMaxStarActionIDs + 1}) {
    TempDir dir;
    std::string empty, original_state, record;
    {
      Core core(dir.path);
      initialize(core);
      empty = read(dir.path + "/history.json");
      run(core, 5000);
      record = completed_record(dir);
      original_state = read(dir.path + "/stars.json");
    }
    std::string history = empty.substr(0, empty.size() - 3);
    const auto item = parse(record);
    for (size_t index = 1; index <= unique_ids; ++index) {
      cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item.get(), "seq"), index);
      cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(item.get(), "duration_ms"),
                          1000 + index);
      const std::string id = "historical-" + std::to_string(index);
      cJSON_ReplaceItemInObject(item.get(), "action_id", cJSON_CreateString(id.c_str()));
      history += (index == 1 ? "" : ",") + encode(item.get()) + "\n";
    }
    history += "]}\n";
    write(dir.path + "/history.json", history);
    write(dir.path + "/actions.json", "{\"schema_version\":1,\"actions\":["
          "{\"id\":\"historical-" + std::to_string(unique_ids) +
          "\",\"name\":\"Selected\"}]}");
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.healthy());
    CHECK(core.record_count() == unique_ids);
    CHECK(core.view(0, true).best_ms == static_cast<int64_t>(1000 + unique_ids));
    CHECK(read(dir.path + "/history.json") == history);
    CHECK(read(dir.path + "/stars.json") == original_state);
    if (unique_ids == timer_app::kMaxStarActionIDs) {
      CHECK(core.update_daily_stars(kDay + 13 * 3600, true));
      stars(core, unique_ids);
      run(core, 100);
      stars(core, unique_ids + 1); // Reusing an ID at the cap is still supported.
    } else {
      CHECK(!core.update_daily_stars(kDay + 13 * 3600, true));
      CHECK(!core.view(0, true).stars_available);
      CHECK(!core.star_error().empty());
      const std::string sentinel = "Do not overwrite this temporary-file sentinel.";
      write(dir.path + "/stars.json.tmp", sentinel);
      for (int day = 0; day < 4; ++day) {
        CHECK(!core.update_daily_stars(kReset + day * 86400, true));
        CHECK(core.healthy());
        CHECK(!core.view(0, true).stars_available);
      }
      CHECK(read(dir.path + "/stars.json.tmp") == sentinel);
      CHECK(read(dir.path + "/stars.json") == original_state);
      run(core, 100, kReset + 10);
      CHECK(core.healthy());
      CHECK(core.record_count() == unique_ids + 1);
      CHECK(core.view(0, true).best_ms == 100);
      CHECK(!core.view(0, true).stars_available);
      CHECK(read(dir.path + "/stars.json.tmp") == sentinel);
      CHECK(read(dir.path + "/stars.json") == original_state);
      CHECK(read(dir.path + "/history.json").compare(0, history.size() - 3,
                                                     history, 0, history.size() - 3) == 0);
      const auto saved = parse(completed_record(dir, static_cast<int>(unique_ids)));
      CHECK(cJSON_GetObjectItemCaseSensitive(saved.get(), "star_generation") == nullptr);
    }
  }
}

void active_run_reset_defers_flash() {
  TempDir dir;
  std::string original_state;
  int64_t old_generation;
  {
    Core core(dir.path);
    initialize(core);
    run(core, 5000);
    original_state = read(dir.path + "/stars.json");
    old_generation = generation(dir);
    CHECK(core.start(1000000, kReset - 2));
    const auto original_pending = read(dir.path + "/pending.json");
    CHECK(core.update_daily_stars(kReset, true));
    stars(core, 0);
    CHECK(core.running());
    CHECK(read(dir.path + "/stars.json") == original_state);
    CHECK(read(dir.path + "/pending.json") == original_pending);
    for (int second = 1; second < 5; ++second) {
      CHECK(!core.update_daily_stars(kReset + second, true));
      CHECK(read(dir.path + "/stars.json") == original_state);
    }
    CHECK(core.stop(4000000, kReset + 1));
    CHECK(core.view(0, true).best_ms == 3000);
    stars(core, 1);
    CHECK(generation(dir) > old_generation);
    const auto saved = parse(completed_record(dir, 1));
    CHECK(number(saved.get(), "star_generation") == generation(dir));
  }
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kReset + 10, true));
    stars(core, 1);
    CHECK(core.record_count() == 2);
    CHECK(core.view(0, true).best_ms == 3000);
  }

  TempDir blocked;
  {
    Core core(blocked.path);
    initialize(core);
    run(core, 5000);
    original_state = read(blocked.path + "/stars.json");
    CHECK(core.start(1000000, kReset - 2));
    CHECK(std::filesystem::create_directory(blocked.path + "/stars.json.tmp"));
    CHECK(core.update_daily_stars(kReset, true));
    stars(core, 0); // No write is attempted even though its destination is blocked.
    CHECK(core.healthy());
    CHECK(core.running());
    CHECK(read(blocked.path + "/stars.json") == original_state);
    CHECK(core.stop(4000000, kReset + 1));
    CHECK(core.healthy());
    CHECK(!core.view(0, true).stars_available);
    CHECK(core.view(0, true).best_ms == 3000);
    CHECK(core.record_count() == 2);
    CHECK(read(blocked.path + "/stars.json") == original_state);
    const auto saved = parse(completed_record(blocked, 1));
    CHECK(number(saved.get(), "duration_ms") == 3000);
    CHECK(cJSON_GetObjectItemCaseSensitive(saved.get(), "star_generation") == nullptr);
    CHECK(std::filesystem::remove(blocked.path + "/stars.json.tmp"));
    CHECK(!core.update_daily_stars(kReset + 2, true));
    CHECK(!core.view(0, true).stars_available);
  }
  {
    Core core(blocked.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kReset + 10, true));
    stars(core, 0);
    CHECK(core.view(0, true).best_ms == 3000);
    CHECK(core.record_count() == 2);
  }
}

void reboot_during_deferred_reset() {
  TempDir dir;
  std::string original_state;
  int64_t original_generation;
  {
    Core core(dir.path);
    initialize(core);
    run(core, 5000);
    original_state = read(dir.path + "/stars.json");
    original_generation = generation(dir);
    CHECK(core.start(1000000, kReset - 1));
    CHECK(core.update_daily_stars(kReset, true));
    stars(core, 0);
    CHECK(read(dir.path + "/stars.json") == original_state);
  }
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(!core.view(0, true).stars_available);
    CHECK(core.record_count() == 2);
    CHECK(core.view(0, true).best_ms == 5000);
    CHECK(core.update_daily_stars(kReset + 2, true));
    stars(core, 0);
    CHECK(generation(dir) > original_generation);
    CHECK(!std::filesystem::exists(dir.path + "/pending.json"));
    const auto interrupted = parse(completed_record(dir, 1));
    const auto *status = cJSON_GetObjectItemCaseSensitive(interrupted.get(), "status");
    CHECK(cJSON_IsString(status));
    CHECK(std::string(status->valuestring) == "interrupted");
    CHECK(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(interrupted.get(), "duration_ms")));
    CHECK(cJSON_GetObjectItemCaseSensitive(interrupted.get(), "star_generation") == nullptr);
  }
}

void cosmetic_write_failure() {
  TempDir dir;
  {
    Core core(dir.path);
    initialize(core);
    run(core, 5000);
    const auto old_state = read(dir.path + "/stars.json");
    CHECK(std::filesystem::create_directory(dir.path + "/stars.json.tmp"));
    CHECK(core.update_daily_stars(kReset, true));
    CHECK(!core.view(0, true).stars_available);
    CHECK(core.healthy());
    CHECK(read(dir.path + "/stars.json") == old_state);
    CHECK(std::filesystem::remove(dir.path + "/stars.json.tmp"));
    for (int second = 1; second <= 5; ++second)
      CHECK(!core.update_daily_stars(kReset + second, true));
    CHECK(read(dir.path + "/stars.json") == old_state); // No busy-loop retries.
    run(core, 4000, kReset + 10);
    CHECK(core.healthy());
    CHECK(core.record_count() == 2);
    CHECK(core.view(0, true).best_ms == 4000);
    CHECK(!core.view(0, true).stars_available);
    const auto record = parse(completed_record(dir, 1));
    CHECK(cJSON_GetObjectItemCaseSensitive(record.get(), "star_generation") == nullptr);
  }
  {
    Core core(dir.path);
    CHECK(core.init());
    CHECK(core.update_daily_stars(kReset + 20, true));
    stars(core, 0);
    CHECK(core.view(0, true).best_ms == 4000);
    run(core, 3000, kReset + 30);
    stars(core, 1);
  }
}
} // namespace

int main() {
  awards_and_activity_changes();
  reset_boundaries_and_power_off();
  clock_changes_and_offsets();
  interrupted_and_completed_recovery();
  missing_stale_and_damaged_state();
  optional_metadata_and_generation_recovery();
  repeated_stop_after_journal_failure();
  metadata_free_v12_history();
  historical_id_cap_is_cosmetic();
  active_run_reset_defers_flash();
  reboot_during_deferred_reset();
  cosmetic_write_failure();
  std::cout << checks << " daily-star assertions passed\n";
}
