#pragma once
#include "clock_view.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
struct cJSON;
namespace timer_app {
constexpr size_t kMaxHistoryBytes = 8 * 1024 * 1024;
constexpr size_t kMaxStarActionIDs = 1024;
struct Action {
  std::string id, name;
  int64_t best_ms = -1;
};
struct View {
  std::string name, error, audio_name;
  size_t index = 0, total = 0;
  bool running = false, clock_valid = false, storage_ok = false;
  bool has_completed_run = false, idle_smile = false;
  int64_t elapsed_ms = 0, best_ms = -1, utc_epoch = 0;
  int utc_offset_minutes = DEFAULT_UTC_OFFSET_MINUTES;
  uint64_t daily_stars = 0;
  bool stars_available = false;
};
class Core {
public:
  explicit Core(std::string root) : root_(std::move(root)) {}
  bool init();
  int utc_offset_minutes() const { return utc_offset_minutes_; }
  bool set_utc_offset_minutes(int minutes);
  bool has_utc_marker() const;
  bool save_utc_marker();
  // Reconcile the local 23:59 reset interval. Returns whether visible stars
  // changed.
  bool update_daily_stars(int64_t utc_epoch, bool clock_valid);
  bool clear_history();
  bool select(int delta);
  bool start(int64_t monotonic_us, int64_t epoch);
  bool stop(int64_t monotonic_us, int64_t epoch);
  bool set_actions(const cJSON *doc);
  cJSON *actions_json() const;
  std::string activities_text() const;
  bool import_activities(std::string text,
                         std::string (*id_for_name)(const std::string &));
  View view(int64_t monotonic_us, bool clock_valid) const;
  const std::string &error() const { return error_; }
  const std::string &star_error() const { return star_error_; }
  const std::string history_path() const { return root_ + "/history.json"; }
  bool running() const { return running_; }
  bool healthy() const { return storage_ok_; }
  size_t record_count() const { return count_; }

private:
  bool load_settings();
  bool load_star_state();
  bool load_actions();
  bool scan_history();
  bool recover_pending();
  bool append_record(const cJSON *record);
  bool atomic_write(const std::string &file, const std::string &text);
  bool fail(const std::string &message);
  void update_stats(const cJSON *record);
  std::string root_, error_, pending_completed_, star_error_;
  std::map<std::string, int64_t> historical_bests_;
  std::vector<Action> actions_;
  size_t selected_ = 0, count_ = 0;
  int64_t started_us_ = 0, started_epoch_ = 0, last_seq_ = 0, active_seq_ = 0,
          frozen_ms_ = -1;
  bool running_ = false, storage_ok_ = false, save_pending_ = false;
  int utc_offset_minutes_ = DEFAULT_UTC_OFFSET_MINUTES;
  int64_t star_generation_ = 0, star_bucket_ = -1, max_star_generation_ = 0;
  uint64_t daily_stars_ = 0;
  bool stars_available_ = false, star_state_ok_ = true, star_replay_ok_ = true,
       star_state_dirty_ = false;
};
bool validate_actions(const cJSON *doc, std::vector<Action> &out,
                      std::string &why);
std::string utc_iso(int64_t epoch);
} // namespace timer_app
