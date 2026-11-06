# Reproducible build

Run `tools/build.sh` to generate USB-flashable images and checksums in the local `dist/` directory.

For source changes, use Espressif's official ESP-IDF v5.5.1 setup instructions. On a Mac, install Xcode command-line tools and the dependencies documented by Espressif. Use the exact tag:

```sh
git clone --branch v5.5.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source export.sh
cd /path/to/note4c-timer
./tools/build.sh
```

Official instructions: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/get-started/index.html

`SDKCONFIG_DEFAULTS` configure ESP32-S3, 16 MiB flash, DIO/80 MHz, octal PSRAM, no console interference with USB serial, 1 kHz FreeRTOS tick. `sdkconfig` and `dependencies.lock` preserve the tested settings and component version. The panel sequence is a standalone implementation from the manufacturer-linked current reference; this project does not require the reference's full ESP-IDF 6 application.

Partition layout:

- 0x000000: bootloader
- 0x008000: partition table
- 0x009000: NVS, 0x6000 bytes
- 0x00f000: PHY init, 0x1000 bytes
- 0x010000: factory app, 0x2f0000 bytes
- 0x300000: LittleFS storage, 0xd00000 bytes

Only the initial install erases the storage range. An ordinary firmware update does not. There is no OTA slot or network updater.

Fonts in `main/fonts.h` and `main/cjk.bin` are generated source assets and are checked in. Regenerating them is optional and requires Pillow plus the DejaVu/Noto files named in `tools/generate_fonts.py`; see licenses. The font binary covers all 20,992 basic CJK codepoints, using 24×32 monochrome glyph cells (about 2 MiB flash).

## Tests

With host C++17 tools, Python3 and the IDF source path available:

```sh
source /path/to/esp-idf/export.sh
./tools/test.sh
```

The tests use host C++ and mocked peripherals. `idf.py build` separately checks the real ESP32-S3 compiler/headers/linker. Neither substitutes for physical-device validation.

## Refresh cadence

The running timer requests elapsed snapshots every second and updates the displayed clock at minute boundaries. While stopped, the clock updates with the next redraw. The display worker coalesces requests and uses the last successfully displayed frame to compute the fast refresh window. The native waveform establishes the first baseline and periodically clears accumulated ghosting.

## Daily award persistence

`Core::update_daily_stars()` reconciles the local 23:59 bucket before input/status and on the main loop; it does no panel I/O. Successful boundary changes queue `Full`, because the footer is outside the clock and elapsed regions. Display requests continue to coalesce while BUSY. Same-interval polling does not write flash. Boundary changes during an active run are applied in RAM and durably committed only after stop captures its timestamp; the old on-disk bucket is safely reconciled after a reboot. Reset writes are atomic and failure-latched to avoid retry storms. Completed award metadata is frozen inside the pending journal before append and is replayed only once per unique history sequence.
