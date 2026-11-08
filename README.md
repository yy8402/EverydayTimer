# EverydayTimer

Custom timer firmware for **ZECTRIX NOTE4C DevKit**, ESP32-S3 N16R8, 400×300 four-color SSD2683 panel. **Not for monochrome NOTE4.** This is a custom development build, not a manufacturer-supported release.

## What it does

- Select an activity with UP or DOWN while stopped. Selection wraps and shows 0 s alongside the saved best.
- Press the front button to start or stop. The LED blinks during a run, and the screen shows READY, RUNNING, or STOPPED.
- Earn a five-point star for a first or strictly faster personal best. Daily stars reset at 23:59 local time.
- After five minutes idle, the timer gives way to a full-screen smile. Waking uses two fast refresh passes to clear its outline from the white timer screen.
- Time is displayed in whole seconds and stored in milliseconds. The timer uses a monotonic clock and keeps a separate wall clock for records.
- Edit activity names in English or Chinese on a removable USB drive. Clock sync and history export use USB serial. The timer works offline.

![Timer renderer preview](docs/timer-preview.png)

## E-paper refresh

Boot establishes a native full-panel baseline. While running, the firmware requests a fast snapshot every second. Fast updates use a volatile monochrome waveform and a window computed from changed pixels. The panel periodically uses its slower native waveform to clear accumulated ghosting. Timing and button handling run independently of the panel refresh.

## Start here on a Mac

1. Read [safe flashing and full backup](docs/FLASHING.md). Confirm the physical model and board revision before any write.
2. Build the images into `dist/` as described in [Build and test](#build-and-test), then use `tools/flash_mac.sh`. It makes and verifies a unique 16 MiB flash backup before writing. Initial `--install` replaces existing firmware and clears the new timer storage area; `--update` preserves this timer's storage layout.
3. Read [USB management](docs/USB.md), list the port, then run `time-sync`. The PCF8563 RTC is set from your Mac in UTC; the Mac’s current offset is saved separately for the local display. Starting is blocked until this timer has successfully saved its first UTC sync (even if previous firmware left a plausible RTC calendar), and thereafter whenever the RTC is invalid, so completed runs are not assigned fake timestamps.
4. Stop the timer, open the **NOTE4C** drive in Finder, and edit `activities.txt` as plain UTF-8 text with one name per line. Save and eject the drive. See [USB management](docs/USB.md#edit-activities-in-finder) for the file rules.

Example clock sync and history export after activating the virtual environment:

```sh
python tools/note4c.py ports
python tools/note4c.py --port /dev/cu.usbmodemXXXX time-sync
python tools/note4c.py --port /dev/cu.usbmodemXXXX history --output history.json
```

## Data and recovery

Keep action IDs stable when renaming or rearranging activities. A new ID deliberately starts a new record category. Removing an action does not erase its historical records; adding the same ID later restores its best. Reusing an old ID for a different activity would mix records and should be avoided.

`history.json` is an ordinary JSON object containing `schema_version` and a `records` array. A completed record looks like:

```json
{"seq":1,"action_id":"activity-1","action_name":"Activity 1","started_at":"2026-11-03T05:00:00Z","ended_at":"2026-11-03T05:00:32Z","duration_ms":32700,"status":"completed"}
```

Start and stop are journaled through a separate internal `pending.json` using LittleFS copy-on-write and checked flushes. After a reset, an unfinished attempt becomes `interrupted` with null duration/end time and is excluded from best records. A completed pending record is recovered without duplicate insertion. A truncated final append is repaired on boot; detected corruption is otherwise preserved and blocks new attempts. A filesystem that will not mount is **never automatically reformatted**, except a completely erased (all-FF) timer storage partition on initial installation. This reduces power-loss risk but is not a promise against all flash failure. Back up important history.

The history cap is 8 MiB within a 13 MiB storage partition. It will stop accepting new runs instead of deleting old ones when full. Export/archive and deliberately reinstall only after backing up if you eventually need a clean history; the USB manager requires explicit confirmation to clear history. Power interruption before the first journal commit or unrecoverable physical flash failure can still lose a run.

The timezone setting defaults to UTC+08:00 and persists in a separate internal `settings.json`. `time-sync` normally replaces it with the Mac’s current offset. You can override with `time-sync --utc-offset +08:00`, or change display timezone without touching the UTC clock using `timezone --utc-offset +08:00`. This is a **fixed offset**, not an IANA timezone or automatic DST rule. Resync after travel or a daylight-saving change. UTC `started_at`/`ended_at` values are never shifted. An unset/invalid clock shows `SYNC CLOCK VIA USB`, not a fabricated date.

The RTC should retain time on the charged battery. Complete battery depletion can invalidate it; the firmware checks the chip's low-voltage flag and requires resync rather than making up a date. UTC timestamps avoid timezone/DST ambiguity. Clock sync is blocked during a run.

## Daily stars

The footer shows one icon per daily star while space permits when it is available and there is no warning. It totals records across all activities, including activities later renamed or removed. The first completed attempt for a previously unrecorded stable action ID earns one star; subsequent attempts must be **strictly faster in saved milliseconds**. Equal times, slower attempts, interrupted runs and unsaved attempts do not earn stars. Distinct millisecond records can therefore earn a star even if their displayed whole seconds look the same on screen.

The daily interval is **[local 23:59, next local 23:59)**. At 23:59, old stars clear first. A record earned at 23:59:30 earns a new star and keeps it through midnight, until the following 23:59. A run spanning the boundary is assigned when it stops. The reset also occurs while idle or running, and is caught up after power-off. While a run is active, it clears in memory without a flash write; stop captures elapsed time first and then persists the new interval before its award. The slow panel may show old stars until the queued full refresh finishes; controls and duration capture do not wait for it.

Each award is part of the same durable completed-run journal/history entry, so reboot or retried recovery cannot award it twice. A small separate `stars.json` tracks the reset interval/generation rather than maintaining a competing count. Existing v1.2 and earlier history still supplies permanent bests but receives no retrospective stars on upgrade. The storage partition and history schema version remain compatible; new completed entries add `new_best` and, for awarded records, `star_generation`/`star_bucket` metadata.

Clock and timezone corrections that cross a 23:59 interval boundary start a fresh star interval, whether the correction moves forward or backward; they do **not** restore old stars. Corrections within the same interval retain stars. No reset is inferred from an invalid clock: the row is blank until a trusted time is available. Zero stars also leaves it blank. Missing/stale reset state starts a fresh generation above all recorded generations, never reviving old awards. Strict replay uses a bounded index of at most 1,024 distinct historical action IDs; exceeding that very high lifetime-ID count disables only stars and reports an error, without removing records or affecting configured-action bests. A damaged reset file is preserved and disables only stars; normal best/history remains usable. A failed star-state write also hides stars and latches an error without repeated writes; restart after resolving the storage issue. Runs saved while stars are unavailable remain permanent records but do not get a retroactive star.

The same persisted fixed UTC offset controls both the displayed clock and the reset time. Resync after travel/DST. `status` exposes `daily_stars`, `stars_available`, `stars_reset_local_time` and any star-state error.

## Build and test

See [BUILD.md](docs/BUILD.md) for the build and host test commands. ESP-IDF **v5.5.1**, target `esp32s3`; pin/component versions are locked. Fonts are pre-generated, so the build does not need Pillow or the original fonts.

## Limitations and first-device checklist

- The firmware has been tested on a physical NOTE4C. Confirm your board revision and complete the checklist below before relying on records.
- The processor stays awake so the timer/button behavior stays simple. Battery endurance has not been measured or optimized; there is no automatic power-off in this version. USB-powered use is the safest first test. The existing power/down key is treated as activity DOWN, not a software power-off command.
- Input is debounced for 20 ms, polled every 5 ms. Millisecond storage is resolution, not a calibrated laboratory accuracy claim. Don't run export/configuration commands while doing a timed attempt. History export intentionally requires a stopped timer.
- A slow screen may show a previous action/state while a new frame is pending. LED blink means running; wait for the selected activity screen before starting if unsure.

First-device test: preserve the factory backup; sync clock; check UP/DOWN ordering, front toggle and LED; verify both English and Chinese labels; compare a 30-second run with a phone stopwatch; tap stop while the panel is actively refreshing; export and check timestamps/duration; reboot and verify best persists; then start and reset to verify an interrupted record. Check RTC again after power cycling. For stars, establish a first best, tie it, improve it, change activities, reboot, then test 23:58:59 → 23:59 and a new win in the last minute. Confirm midnight does not clear that new star and permanent records remain unchanged. Keep the backup until all checks pass.

## Wireless Codex Voice

Firmware 1.11.0 keeps timer taps on the front button. A hold of about one second
sends an authenticated Voice event over Wi-Fi. The Mac bridge discovers the
board on the same local network and reconnects after a disconnect. Audio
uses the local network with no cable after pairing. The audio stream is
unencrypted, so use a trusted network.

After flashing, pair once with a USB data cable.

```sh
python -m pip install -r tools/requirements-live-audio.txt
python tools/wifi_audio.py --pair --ssid "YOUR NETWORK" --key-file ~/.note4c-wifi.key
```

Install BlackHole 2ch and 16ch, then use them as the Mac's input and output
respectively. Install and activate Karabiner VirtualHIDDevice 8.0.0 and run
its daemon. Build the fixed-chord helper using the headers from the matching
release checkout.

```sh
clang++ -std=c++23 -O2 -I "$KARABINER_SOURCE/include"   -isystem "$KARABINER_SOURCE/vendor/vendor/include"   -framework IOKit -framework CoreFoundation tools/voice_button.cpp   -o build/note4c-voice-button
sudo -v
python tools/wifi_audio.py --live --key-file ~/.note4c-wifi.key --voice-events   | sudo -n build/note4c-voice-button
```

The network bridge runs as your Mac user. The privileged helper reads only
the fixed Voice event from its local pipe and sends Option–Shift–V. Set that
chord in Codex Voice settings. Replies default to 5× nominal gain.

### Change Wi-Fi after pairing

Firmware 1.12.0 accepts the USB `wifi_config` command with `ssid` and
`password`. Use VoiceLink's NOTE4C `pair.py --reconfigure` command to enter
the password at a hidden prompt. Restart the board after a successful save.
The command preserves the VoiceLink key and timer data.

### Demo activity

Demo appears first and is selected at startup and when waking from the idle
screen. The first button tap wakes the timer, and the next tap can start
Demo. It times a run normally and shows the current daily stars. Demo runs stay out of history, do not set
a best time, and do not award stars. Demo is built in and does not appear
in the editable activity file. For saved activities, BEST and LAST show the
fastest and most recent completed times.

### Export or clear history from Finder

The NOTE4C volume exports a UTF-8 `history.csv` snapshot when it mounts.
Copy it to your Mac before clearing history. To clear the board's history,
stop the timer, move `history.csv` to Trash, and eject NOTE4C in Finder.
The board clears history only after a clean eject of a volume that received
a complete export and only if no records changed after that export. On
success, it recreates a header-only CSV. Clearing history also clears the
best times and daily stars derived from it.

Unplugging the cable, a missing or failed export, and editing CSV contents
leave internal history intact.

#### Recover a missing export without clearing records

1. Stop the timer and save a complete backup from the project directory with
   `python tools/note4c.py history --output history-backup.json`. Save it on
   the Mac outside the NOTE4C volume.
2. Check free space on NOTE4C. Finder's Trash can still hold the previous
   export on the device. If you need to free that space, copy the trashed
   CSV to the Mac first, then select just that item in Trash and choose
   **Delete Immediately**. You can also move other files off the volume.
3. If a `history.csv` is already visible at the root of NOTE4C, leave it in
   place when ejecting. Cleanly eject the volume, then reconnect or restart
   the board. It publishes a fresh snapshot on the next mount.
4. Open the new CSV and check its row count against the USB backup. If the
   board still reports an export error or the history is too large for
   the volume, use the complete JSON backup from step 1.
