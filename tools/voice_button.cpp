#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <pqrs/karabiner/driverkit/virtual_hid_device_driver.hpp>
#include <pqrs/karabiner/driverkit/virtual_hid_device_service.hpp>

namespace driver = pqrs::karabiner::driverkit::virtual_hid_device_driver;
namespace service = pqrs::karabiner::driverkit::virtual_hid_device_service;

int main() {
  using namespace std::chrono_literals;
  pqrs::dispatcher::extra::initialize_shared_dispatcher();
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
    service::client keyboard;
    keyboard.connected.connect([&] {
      service::virtual_hid_keyboard_parameters parameters;
      parameters.set_country_code(pqrs::hid::country_code::us);
      keyboard.async_virtual_hid_keyboard_initialize(parameters);
    });
    keyboard.virtual_hid_keyboard_ready.connect([&](bool value) {
      std::lock_guard lock(mutex);
      ready = value;
      changed.notify_all();
    });
    keyboard.async_start();
    {
      std::unique_lock lock(mutex);
      if (!changed.wait_for(lock, 8s, [&] { return ready; })) {
        std::cerr << "Virtual keyboard did not become ready. Check the driver and daemon.\n";
        return 1;
      }
    }
    std::cerr << "NOTE4C virtual keyboard ready.\n";
    std::string line;
    using namespace driver::hid_report;
    keyboard_input modifiers;
    modifiers.modifiers.insert(modifier::left_option);
    modifiers.modifiers.insert(modifier::left_shift);
    auto chord = modifiers;
    chord.keys.insert(type_safe::get(pqrs::hid::usage::keyboard_or_keypad::keyboard_v));
    while (std::getline(std::cin, line)) {
      if (line != "voice")
        continue;
      for (const auto &report : {modifiers, chord, modifiers, keyboard_input{}}) {
        keyboard.async_post_report(report);
        std::this_thread::sleep_for(60ms);
      }
    }
  }
  pqrs::dispatcher::extra::terminate_shared_dispatcher();
}
