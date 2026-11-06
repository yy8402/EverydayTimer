#include "core.h"
#include "cJSON.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
namespace timer_app {
static constexpr const char *kHeader = "{\"schema_version\":1,\"records\":[\n";
static constexpr const char *kFooter = "]}\n";
static const char *str(const cJSON *j, const char *k) {
  const auto *x = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(x) ? x->valuestring : "";
}
static double num(const cJSON *j, const char *k, double fallback = 0) {
  const auto *x = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsNumber(x) ? x->valuedouble : fallback;
}
static std::string encoded(const cJSON *j) {
  char *p = cJSON_PrintUnformatted(j);
  if (!p)
    return {};
  std::string s(p);
  cJSON_free(p);
  return s;
}
static cJSON *read_json(const std::string &path, size_t max = 8192) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return nullptr;
  std::string s;
  char b[256];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f))) {
    s.append(b, n);
    if (s.size() > max) {
      fclose(f);
      return nullptr;
    }
  }
  bool ok = !ferror(f);
  fclose(f);
  return ok ? cJSON_ParseWithLength(s.c_str(), s.size() + 1) : nullptr;
}
bool validate_actions(const cJSON *doc, std::vector<Action> &out,
                      std::string &why) {
  auto reject = [&](const char *s) {
    why = s;
    return false;
  };
  if (!cJSON_IsObject(doc) || num(doc, "schema_version") != 1)
    return reject("schema_version must be 1");
  const cJSON *a = cJSON_GetObjectItemCaseSensitive(doc, "actions");
  int n = cJSON_GetArraySize(a);
  if (!cJSON_IsArray(a) || n < 1 || n > 32)
    return reject("actions must contain 1 to 32 entries");
  out.clear();
  const cJSON *x;
  cJSON_ArrayForEach(x, a) {
    std::string id = str(x, "id"), name = str(x, "name");
    if (!cJSON_IsObject(x) || id.empty() || id.size() > 32 || name.empty() ||
        name.size() > 72)
      return reject(
          "id/name missing or too long (32 ID characters / 72 UTF-8 bytes)");
    for (char c : id)
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
            c == '-'))
        return reject("id must use lowercase a-z, 0-9, _ or -");
    bool nonspace = false;
    size_t chars = 0;
    for (size_t i = 0; i < name.size();) {
      unsigned char c = name[i];
      if (c >= 32 && c <= 126) {
        if (c != ' ')
          nonspace = true;
        i++;
      } else if (c >= 0xe4 && c <= 0xe9 && i + 2 < name.size() &&
                 (static_cast<unsigned char>(name[i + 1]) & 0xc0) == 0x80 &&
                 (static_cast<unsigned char>(name[i + 2]) & 0xc0) == 0x80) {
        uint32_t cp =
            ((c & 15) << 12) | ((name[i + 1] & 63) << 6) | (name[i + 2] & 63);
        if (cp < 0x4e00 || cp > 0x9fff)
          return reject("Unsupported character; use English or basic Chinese");
        nonspace = true;
        i += 3;
      } else
        return reject("Unsupported character; use English or basic Chinese");
      if (++chars > 24)
        return reject("name exceeds 24 characters");
    }
    if (!nonspace)
      return reject("name cannot be blank");
    for (const auto &prev : out)
      if (prev.id == id)
        return reject("duplicate action id");
    out.push_back({id, name});
  }
  return true;
}
std::string utc_iso(int64_t epoch) {
  if (epoch < 1704067200LL || epoch > 4102444799LL)
    return {};
  time_t t = epoch;
  tm z{};
  gmtime_r(&t, &z);
  char b[24];
  strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &z);
  return b;
}
bool Core::fail(const std::string &s) {
  error_ = s;
  return false;
}
bool Core::atomic_write(const std::string &file, const std::string &text) {
  std::string p = root_ + "/" + file, tmp = p + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f)
    return fail("Cannot open temporary " + file);
  bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  if (fflush(f) != 0)
    ok = false;
  if (fsync(fileno(f)) != 0)
    ok = false;
  if (fclose(f) != 0)
    ok = false;
  if (!ok)
    return fail("Storage write failed: " + file);
  if (rename(tmp.c_str(), p.c_str()) != 0)
    return fail("Storage rename failed: " + file);
  return true;
}
bool Core::load_actions() {
  std::string p = root_ + "/actions.json";
  struct stat st{};
  if (stat(p.c_str(), &st) != 0) {
    if (errno != ENOENT)
      return fail("Cannot read actions.json");
    if (!atomic_write(
            "actions.json",
            "{\"schema_version\":1,\"actions\":[{\"id\":\"activity-1\","
            "\"name\":\"Activity "
            "1\"},{\"id\":\"activity-2\",\"name\":\"Activity "
            "2\"},{\"id\":\"activity-3\",\"name\":\"Activity 3\"}]}\n"))
      return false;
  }
  cJSON *d = read_json(p);
  std::string why;
  bool ok = validate_actions(d, actions_, why);
  cJSON_Delete(d);
  return ok ? true : fail("Invalid actions.json: " + why);
}
// Optional cosmetic metadata never invalidates the permanent run itself.
static bool star_metadata(const cJSON *r, int64_t &generation,
                          int64_t &bucket) {
  const auto *g = cJSON_GetObjectItemCaseSensitive(r, "star_generation");
  const auto *b = cJSON_GetObjectItemCaseSensitive(r, "star_bucket");
  if (!cJSON_IsNumber(g) || !cJSON_IsNumber(b) ||
      !std::isfinite(g->valuedouble) || !std::isfinite(b->valuedouble) ||
      floor(g->valuedouble) != g->valuedouble ||
      floor(b->valuedouble) != b->valuedouble || g->valuedouble < 1 ||
      g->valuedouble >= 9007199254740991.0 || b->valuedouble < 0 ||
      b->valuedouble > 50000)
    return false;
  generation = (int64_t)g->valuedouble;
  bucket = (int64_t)b->valuedouble;
  return true;
}
void Core::update_stats(const cJSON *r) {
  const char *id = str(r, "action_id");
  if (strcmp(str(r, "status"), "completed") != 0)
    return;
  int64_t duration = (int64_t)num(r, "duration_ms", -1);
  if (duration < 0)
    return;
  auto previous = historical_bests_.find(id);
  if (star_replay_ok_ && previous == historical_bests_.end() &&
      historical_bests_.size() >= kMaxStarActionIDs) {
    star_replay_ok_ = false;
    stars_available_ = false;
    star_error_ = "Stars unavailable: over 1024 historical activity IDs";
    historical_bests_.clear();
    previous = historical_bests_.end();
  }
  const bool new_best =
      star_replay_ok_ &&
      (previous == historical_bests_.end() || duration < previous->second);
  if (new_best)
    historical_bests_[id] = duration;
  int64_t generation = 0, bucket = 0;
  if (star_metadata(r, generation, bucket)) {
    max_star_generation_ = std::max(max_star_generation_, generation);
    if (new_best &&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "new_best")) &&
        generation == star_generation_ && bucket == star_bucket_)
      ++daily_stars_;
  }
  for (auto &a : actions_)
    if (a.id == id && (a.best_ms < 0 || duration < a.best_ms))
      a.best_ms = duration;
}
static bool iso_timestamp(const char *s) {
  if (strlen(s) != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
      s[13] != ':' || s[16] != ':' || s[19] != 'Z')
    return false;
  for (int i = 0; i < 19; i++)
    if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 &&
        (s[i] < '0' || s[i] > '9'))
      return false;
  return true;
}
static bool valid_record(const cJSON *r, bool pending) {
  if (!cJSON_IsObject(r))
    return false;
  const double seq = num(r, "seq", -1);
  if (!std::isfinite(seq) || seq < 1 || seq > 9007199254740991.0 ||
      floor(seq) != seq)
    return false;
  const char *id = str(r, "action_id");
  const char *label = str(r, "action_name");
  if (!*id || strlen(id) > 32 || !*label || strlen(label) > 72 ||
      !iso_timestamp(str(r, "started_at")))
    return false;
  for (const char *p = id; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' ||
          *p == '-'))
      return false;
  const char *state = str(r, "status");
  if (pending && strcmp(state, "running") == 0)
    return true;
  const auto *duration = cJSON_GetObjectItemCaseSensitive(r, "duration_ms");
  if (strcmp(state, "completed") == 0)
    return cJSON_IsNumber(duration) && std::isfinite(duration->valuedouble) &&
           duration->valuedouble >= 0 &&
           duration->valuedouble < 9007199254740991.0 &&
           floor(duration->valuedouble) == duration->valuedouble &&
           iso_timestamp(str(r, "ended_at"));
  return !pending && strcmp(state, "interrupted") == 0 &&
         cJSON_IsNull(duration) &&
         cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(r, "ended_at"));
}
bool Core::scan_history() {
  historical_bests_.clear();
  star_replay_ok_ = true;
  daily_stars_ = 0;
  max_star_generation_ = 0;
  for (auto &a : actions_) {
    a.best_ms = -1;
  }
  count_ = 0;
  last_seq_ = 0;
  const auto p = history_path();
  struct stat st{};
  if (stat(p.c_str(), &st) != 0) {
    if (errno != ENOENT)
      return fail("Cannot stat history.json");
    if (!atomic_write("history.json", std::string(kHeader) + kFooter))
      return false;
  }
  FILE *f = fopen(p.c_str(), "r+b");
  if (!f)
    return fail("Cannot open history.json");
  char line[1024];
  if (!fgets(line, sizeof line, f) || strcmp(line, kHeader) != 0) {
    fclose(f);
    return fail("History header damaged; export/backup before repair");
  }
  long last_good = ftell(f);
  bool footer = false, damaged = false;
  int64_t prev = 0;
  while (fgets(line, sizeof line, f)) {
    if (strcmp(line, kFooter) == 0) {
      footer = true;
      break;
    }
    char *payload = line;
    const bool correct_comma =
        (count_ == 0) ? payload[0] != ',' : payload[0] == ',';
    if (payload[0] == ',')
      ++payload;
    size_t len = strlen(payload);
    if (len == 0 || payload[len - 1] != '\n') {
      damaged = true;
      break;
    }
    cJSON *r = cJSON_ParseWithOpts(payload, nullptr, true);
    const bool record_ok = valid_record(r, false);
    const int64_t seq = record_ok ? (int64_t)num(r, "seq", -1) : -1;
    bool valid = correct_comma && record_ok && seq > prev;
    if (!valid) {
      cJSON_Delete(r);
      damaged = true;
      break;
    }
    prev = seq;
    last_seq_ = seq;
    count_++;
    update_stats(r);
    cJSON_Delete(r);
    last_good = ftell(f);
  }
  if (ferror(f)) {
    fclose(f);
    return fail("History read failed");
  }
  if (!footer) {
    // Only repair an interrupted final append. Never skip corrupt interior
    // rows.
    bool extra = false;
    while (fgets(line, sizeof line, f))
      if (line[0] == '{' || line[0] == ',' || strcmp(line, kFooter) == 0)
        extra = true;
    if (extra) {
      fclose(f);
      return fail("History interior damaged; preserved for recovery");
    }
    if (ftruncate(fileno(f), last_good) != 0 ||
        fseek(f, last_good, SEEK_SET) != 0 || fputs(kFooter, f) == EOF ||
        fflush(f) != 0 || fsync(fileno(f)) != 0) {
      fclose(f);
      return fail("History tail repair failed");
    }
  }
  if (footer && fgetc(f) != EOF) {
    fclose(f);
    return fail("Unexpected data after history footer; preserved");
  }
  (void)damaged;
  fclose(f);
  return true;
}
bool Core::append_record(const cJSON *r) {
  auto s = encoded(r);
  if (s.empty() || s.size() > 900)
    return fail("Record encoding failed");
  FILE *f = fopen(history_path().c_str(), "r+b");
  if (!f)
    return fail("Cannot open history to save run");
  bool ok = fseek(f, 0, SEEK_END) == 0;
  long bytes = ftell(f);
  if (bytes < 3 || (size_t)bytes + s.size() + 2 > kMaxHistoryBytes) {
    fclose(f);
    return fail("History full; export and archive before adding runs");
  }
  ok = ok && fseek(f, -3, SEEK_END) == 0;
  const std::string addition = (count_ ? "," : "") + s + "\n" + kFooter;
  ok = ok && fwrite(addition.data(), 1, addition.size(), f) == addition.size();
  if (fflush(f) != 0)
    ok = false;
  if (fsync(fileno(f)) != 0)
    ok = false;
  if (fclose(f) != 0)
    ok = false;
  if (!ok) {
    storage_ok_ = false;
    return fail("Run not confirmed saved; restart to recover pending record");
  }
  count_++;
  last_seq_ = (int64_t)num(r, "seq");
  update_stats(r);
  return true;
}
bool Core::recover_pending() {
  const auto p = root_ + "/pending.json";
  struct stat st{};
  if (stat(p.c_str(), &st) != 0)
    return errno == ENOENT ? true : fail("Cannot inspect pending run");
  cJSON *r = read_json(p);
  if (!valid_record(r, true)) {
    cJSON_Delete(r);
    return fail("Pending run damaged; preserved for recovery");
  }
  const int64_t seq = (int64_t)num(r, "seq", -1);
  if (seq <= 0 || seq < last_seq_) {
    cJSON_Delete(r);
    return fail("Invalid pending sequence");
  }
  if (seq > last_seq_) {
    if (strcmp(str(r, "status"), "completed") != 0) {
      cJSON_ReplaceItemInObject(r, "status", cJSON_CreateString("interrupted"));
      cJSON_AddNullToObject(r, "duration_ms");
      cJSON_AddNullToObject(r, "ended_at");
      cJSON_AddStringToObject(
          r, "reason", "Device restarted before stop; not used for best");
    }
    if (!append_record(r)) {
      cJSON_Delete(r);
      return false;
    }
  }
  cJSON_Delete(r);
  if (unlink(p.c_str()) != 0)
    return fail("Cannot clear recovered pending run");
  return true;
}
bool Core::has_utc_marker() const {
  cJSON *doc = read_json(root_ + "/clock.json", 128);
  bool valid = cJSON_IsObject(doc) && num(doc, "schema_version") == 1 &&
               strcmp(str(doc, "basis"), "UTC") == 0;
  cJSON_Delete(doc);
  return valid;
}
bool Core::save_utc_marker() {
  if (!storage_ok_)
    return fail("Storage unavailable; cannot establish UTC clock");
  return atomic_write("clock.json",
                      "{\"schema_version\":1,\"basis\":\"UTC\"}\n");
}
bool Core::load_settings() {
  const auto path = root_ + "/settings.json";
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) {
    if (errno != ENOENT)
      return fail("Cannot read timezone settings");
    return atomic_write("settings.json",
                        "{\"schema_version\":1,\"utc_offset_minutes\":480}\n");
  }
  cJSON *doc = read_json(path, 256);
  const double value = num(doc, "utc_offset_minutes", 9999);
  const bool valid = cJSON_IsObject(doc) && num(doc, "schema_version") == 1 &&
                     std::isfinite(value) && floor(value) == value &&
                     value >= -720 && value <= 840;
  if (valid)
    utc_offset_minutes_ = (int)value;
  cJSON_Delete(doc);
  return valid ? true
               : fail("Invalid timezone settings; preserved for recovery");
}
bool Core::set_utc_offset_minutes(int minutes) {
  if (!storage_ok_)
    return fail("Storage unavailable; cannot save timezone");
  if (running_ || save_pending_)
    return fail("Stop and save before changing timezone");
  if (!valid_utc_offset(minutes))
    return fail("UTC offset must be -720 through +840 minutes");
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddNumberToObject(doc, "schema_version", 1);
  cJSON_AddNumberToObject(doc, "utc_offset_minutes", minutes);
  const bool ok = atomic_write("settings.json", encoded(doc) + "\n");
  cJSON_Delete(doc);
  if (ok) {
    utc_offset_minutes_ = minutes;
    error_.clear();
  }
  return ok;
}
bool Core::load_star_state() {
  const auto path = root_ + "/stars.json";
  struct stat st{};
  if (stat(path.c_str(), &st) != 0) {
    if (errno != ENOENT) {
      star_state_ok_ = false;
      star_error_ = "Stars unavailable: cannot read reset state";
    }
    return true; // Cosmetic failure must not discard or block run history.
  }
  cJSON *doc = read_json(path, 256);
  const double g = num(doc, "generation", -1), b = num(doc, "bucket", -1);
  const bool valid = cJSON_IsObject(doc) && num(doc, "schema_version") == 1 &&
                     std::isfinite(g) && std::isfinite(b) && floor(g) == g &&
                     floor(b) == b && g >= 1 && g < 9007199254740991.0 &&
                     b >= 0 && b <= 50000;
  if (valid) {
    star_generation_ = (int64_t)g;
    star_bucket_ = (int64_t)b;
  } else {
    star_state_ok_ = false;
    star_error_ = "Stars unavailable: reset state damaged; preserved";
  }
  cJSON_Delete(doc);
  return true;
}
bool Core::update_daily_stars(int64_t utc_epoch, bool clock_valid) {
  const bool was_available = stars_available_;
  const uint64_t previous_count = daily_stars_;
  if (!clock_valid || utc_epoch < 1704067200LL || utc_epoch > 4102444799LL ||
      !storage_ok_ || !star_state_ok_ || !star_replay_ok_) {
    stars_available_ = false;
    return was_available;
  }
  // A daily interval starts at local 23:59, including that final minute.
  const int64_t bucket =
      (utc_epoch + int64_t(utc_offset_minutes_) * 60 + 60) / 86400;
  if (star_generation_ == 0 || bucket != star_bucket_ ||
      star_generation_ < max_star_generation_) {
    const int64_t next = std::max(star_generation_, max_star_generation_) + 1;
    if (next >= 9007199254740991LL) {
      stars_available_ = false;
      star_state_ok_ = false;
      star_error_ = "Stars unavailable: reset generation exhausted";
      return was_available;
    }
    star_generation_ = next;
    star_bucket_ = bucket;
    daily_stars_ = 0;
    star_state_dirty_ = true;
  }
  // Never perform the nightly fsync in the active timer's input loop. The
  // visible reset is immediate; stop freezes elapsed time before committing
  // this state and its award. A reboot with the old on-disk bucket catches up.
  if (star_state_dirty_ && !running_) {
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc, "schema_version", 1);
    cJSON_AddNumberToObject(doc, "generation", star_generation_);
    cJSON_AddNumberToObject(doc, "bucket", star_bucket_);
    const std::string old_error = error_;
    const bool ok = atomic_write("stars.json", encoded(doc) + "\n");
    cJSON_Delete(doc);
    if (!ok) {
      star_error_ = "Stars unavailable: " + error_;
      error_ = old_error;
      stars_available_ = false;
      star_state_ok_ = false; // No tight-loop writes or automatic retry storm.
      return was_available;
    }
    star_state_dirty_ = false;
  }
  stars_available_ = true;
  star_error_.clear();
  return !was_available || previous_count != daily_stars_;
}
bool Core::init() {
  storage_ok_ = load_settings() && load_actions() && load_star_state() &&
                scan_history() && recover_pending();
  if (storage_ok_)
    error_.clear();
  return storage_ok_;
}
bool Core::clear_history() {
  if (!storage_ok_ || running_ || save_pending_)
    return fail("Stop and save the timer before clearing history");
  struct stat pending{};
  if (stat((root_ + "/pending.json").c_str(), &pending) == 0 || errno != ENOENT)
    return fail("Restart to recover the pending run before clearing history");
  if (!atomic_write("history.json", std::string(kHeader) + kFooter))
    return false;
  storage_ok_ = scan_history();
  if (storage_ok_)
    error_.clear();
  if (storage_ok_)
    frozen_ms_ = -1;
  return storage_ok_;
}

bool Core::select(int delta) {
  if (running_ || save_pending_)
    return fail("Stop and save current run before changing activity");
  if (actions_.empty())
    return false;
  selected_ = (selected_ + actions_.size() + delta) % actions_.size();
  frozen_ms_ = -1;
  error_.clear();
  return true;
}
bool Core::start(int64_t mono, int64_t epoch) {
  if (!storage_ok_ || save_pending_)
    return fail("Storage needs attention; new run blocked");
  if (running_)
    return fail("Already running");
  if (utc_iso(epoch).empty())
    return fail("Sync clock over USB before first run");
  update_daily_stars(epoch, true);
  struct stat st{};
  if (stat(history_path().c_str(), &st) != 0 ||
      (size_t)st.st_size + 2048 > kMaxHistoryBytes)
    return fail("History full; export and archive first");
  const auto &a = actions_[selected_];
  active_seq_ = last_seq_ + 1;
  started_epoch_ = epoch;
  started_us_ = mono;
  cJSON *r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "seq", active_seq_);
  cJSON_AddStringToObject(r, "action_id", a.id.c_str());
  cJSON_AddStringToObject(r, "action_name", a.name.c_str());
  cJSON_AddStringToObject(r, "started_at", utc_iso(epoch).c_str());
  cJSON_AddStringToObject(r, "status", "running");
  bool ok = atomic_write("pending.json", encoded(r) + "\n");
  cJSON_Delete(r);
  if (!ok)
    return false;
  running_ = true;
  error_.clear();
  return true;
}
bool Core::stop(int64_t mono, int64_t epoch) {
  if (!running_ && !save_pending_)
    return fail("Not running");
  if (running_) {
    frozen_ms_ = std::max<int64_t>(0, (mono - started_us_) / 1000);
    running_ = false;
    save_pending_ = true;
    update_daily_stars(epoch, !utc_iso(epoch).empty());
    const auto &a = actions_[selected_];
    const bool new_best = a.best_ms < 0 || frozen_ms_ < a.best_ms;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "seq", active_seq_);
    cJSON_AddStringToObject(r, "action_id", a.id.c_str());
    cJSON_AddStringToObject(r, "action_name", a.name.c_str());
    cJSON_AddStringToObject(r, "started_at", utc_iso(started_epoch_).c_str());
    cJSON_AddStringToObject(r, "ended_at", utc_iso(epoch).c_str());
    cJSON_AddNumberToObject(r, "duration_ms", frozen_ms_);
    cJSON_AddStringToObject(r, "status", "completed");
    cJSON_AddBoolToObject(r, "new_best", new_best);
    if (new_best && stars_available_) {
      cJSON_AddNumberToObject(r, "star_generation", star_generation_);
      cJSON_AddNumberToObject(r, "star_bucket", star_bucket_);
    }
    pending_completed_ = encoded(r) + "\n";
    cJSON_Delete(r);
  }
  if (!atomic_write("pending.json", pending_completed_)) {
    storage_ok_ = false;
    return false;
  }
  if (!scan_history() || !recover_pending()) {
    storage_ok_ = false;
    return false;
  }
  save_pending_ = false;
  pending_completed_.clear();
  storage_ok_ = true;
  error_.clear();
  return true;
}
static cJSON *actions_document(const std::vector<Action> &actions) {
  cJSON *d = cJSON_CreateObject();
  cJSON_AddNumberToObject(d, "schema_version", 1);
  cJSON *a = cJSON_AddArrayToObject(d, "actions");
  for (const auto &x : actions) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "id", x.id.c_str());
    cJSON_AddStringToObject(r, "name", x.name.c_str());
    cJSON_AddItemToArray(a, r);
  }
  return d;
}
cJSON *Core::actions_json() const { return actions_document(actions_); }

std::string Core::activities_text() const {
  std::string text;
  for (const auto &action : actions_)
    text += action.name + '\n';
  return text;
}

bool Core::import_activities(std::string text,
                             std::string (*id_for_name)(const std::string &)) {
  if (text.compare(0, 3, "\xef\xbb\xbf") == 0)
    text.erase(0, 3);
  if (text.find('\0') != std::string::npos)
    return fail("Use plain UTF-8 text");
  std::vector<Action> next;
  size_t begin = 0;
  while (begin < text.size()) {
    size_t end = text.find('\n', begin);
    auto name = text.substr(begin, end - begin);
    if (!name.empty() && name.back() == '\r')
      name.pop_back();
    if (!name.empty()) {
      for (const auto &action : next)
        if (action.name == name)
          return fail("Duplicate activity name");
      if (next.size() == 32)
        return fail("Use 1 to 32 activities");
      auto existing = std::find_if(actions_.begin(), actions_.end(),
                                   [&](const Action &a) { return a.name == name; });
      next.push_back({existing == actions_.end() ? id_for_name(name) : existing->id,
                      name});
    }
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }

  if (next.size() == actions_.size() &&
      std::equal(next.begin(), next.end(), actions_.begin(),
                 [](const Action &a, const Action &b) { return a.id == b.id && a.name == b.name; }))
    return true;

  auto *doc = actions_document(next);
  bool ok = set_actions(doc);
  cJSON_Delete(doc);
  return ok;
}

bool Core::set_actions(const cJSON *d) {
  if (running_ || save_pending_)
    return fail("Stop and save before editing actions");
  if (!storage_ok_)
    return fail("Storage needs attention");
  std::vector<Action> next;
  std::string why;
  if (!validate_actions(d, next, why))
    return fail(why);
  if (!atomic_write("actions.json", encoded(d) + "\n"))
    return false;
  actions_ = next;
  frozen_ms_ = -1;
  selected_ = 0;
  if (!scan_history()) {
    storage_ok_ = false;
    return false;
  }
  error_.clear();
  return true;
}
View Core::view(int64_t mono, bool clock_valid) const {
  View v;
  v.index = selected_;
  v.total = actions_.size();
  v.clock_valid = clock_valid;
  v.utc_offset_minutes = utc_offset_minutes_;
  v.running = running_;
  v.storage_ok = storage_ok_;
  v.error = error_.empty() ? star_error_ : error_;
  v.stars_available =
      stars_available_ && clock_valid && storage_ok_ && star_replay_ok_;
  v.daily_stars = v.stars_available ? daily_stars_ : 0;
  if (!actions_.empty()) {
    const auto &a = actions_[selected_];
    v.name = a.name;
    v.has_completed_run = frozen_ms_ >= 0;
    v.best_ms = a.best_ms;
    v.elapsed_ms = running_ ? std::max<int64_t>(0, (mono - started_us_) / 1000)
                            : std::max<int64_t>(0, frozen_ms_);
  }
  return v;
}
} // namespace timer_app
