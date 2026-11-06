#include "core.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
using timer_app::Core;
static int checks = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      std::cerr << "State view failed at " << __LINE__ << ": " << #x << '\n';  \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
int main() {
  char pattern[] = "/tmp/note4c-state-XXXXXX";
  const char *p = mkdtemp(pattern);
  CHECK(p != nullptr);
  const std::string root(p);
  constexpr int64_t epoch = 1793707200;
  {
    Core core(root);
    CHECK(core.init());
    CHECK(!core.view(0, true).running);
    CHECK(!core.view(0, true).has_completed_run);
    CHECK(core.start(1000000, epoch));
    CHECK(core.view(1000000, true).running);
    CHECK(!core.view(1000000, true).has_completed_run);
    CHECK(core.stop(1000000, epoch)); // A legitimate 0 ms completed benchmark.
    CHECK(core.view(0, true).has_completed_run);
    CHECK(!core.view(0, true).running);
    CHECK(core.view(0, true).best_ms == 0);
    CHECK(core.view(0, true).elapsed_ms == 0);
    CHECK(core.select(1));
    CHECK(!core.view(0, true).has_completed_run);
    CHECK(core.start(2000000, epoch + 1)); // Interrupt an unrecorded activity.
  }
  {
    Core core(root);
    CHECK(core.init());
    CHECK(!core.view(0, true).has_completed_run);
    CHECK(core.view(0, true).best_ms == 0); // Zero benchmark survives boot.
    CHECK(core.select(1));
    CHECK(
        !core.view(0, true).has_completed_run); // Interrupted isn't completed.
    CHECK(core.start(3000000, epoch + 2));
    CHECK(std::filesystem::create_directory(root + "/pending.json.tmp"));
    CHECK(!core.stop(4000000, epoch + 3));
    CHECK(!core.view(0, true).running);
    CHECK(core.view(0, true)
              .has_completed_run); // Frozen, unsaved attempt isn't READY.
    CHECK(!core.view(0, true).error.empty());
    CHECK(std::filesystem::remove(root + "/pending.json.tmp"));
    CHECK(core.stop(9000000, epoch + 8));
    CHECK(core.view(0, true).has_completed_run);
    CHECK(core.view(0, true).elapsed_ms == 1000);
    CHECK(core.start(10000000, epoch + 9));
    CHECK(core.view(0, true).running);
    CHECK(core.view(0, true).has_completed_run);
    CHECK(core.stop(11000000, epoch + 10));
  }
  std::filesystem::remove_all(root);
  std::cout << checks << " state-view assertions passed\n";
}
