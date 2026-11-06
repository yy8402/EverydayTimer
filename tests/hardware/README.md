# Hardware-layer host tests

Run from any directory:

```sh
./tests/hardware/run.sh
```

Requires a C++17 compiler. Set `CXX` to choose one. The script creates and removes a temporary build directory and does not connect to any device.

The test compiles the actual `main/hardware.cpp` against small, original ESP-IDF API mocks in `stubs/`. It checks:

- Every midnight from 2000-01-01 through 2099-12-31: **36,525 UTC set/read round trips**. Host `gmtime_r` supplies an independent date conversion for the firmware's read algorithm.
- The 2038 boundary, minimum/maximum accepted epochs, leap days, invalid BCD, invalid calendar fields, RTC voltage-low flag, STOP, test mode and unsupported century.
- I2C read/write failures, failure during the seven-byte time write, and failure when restarting the RTC. Incomplete writes remain stopped and cannot be read as valid time.
- Native panel success, permanently asserted BUSY, missing BUSY assertion, SPI failure, recovery and panel power/bus cleanup.
- Power latch high, shared I2C supply high, amplifier and NFC disabled, active-low LED initially off and then controllable, and USB GPIO19/20 untouched.

These are **logic and error-path tests**, not real ESP-IDF compilation, real electrical verification, screen-quality measurements or battery-life tests. The mock clock and BUSY signal do not reproduce physical panel timing. A successful run does not prove that a connected device will refresh.

See `docs/HARDWARE_VALIDATION.md` for sources and the required physical checks.
