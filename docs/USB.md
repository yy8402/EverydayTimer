# USB management (macOS)

The timer does not need a network connection. The NOTE4C drive exposes the
activity list and optional sound effects. Clock settings and history use USB serial. These instructions assume the timer firmware is installed.
Connect the Note4C with a data-capable USB cable and close serial monitors before
using the manager. Commands below run from this project's folder.

## One-time setup

Install Python 3 if needed. Create a virtual environment and install the pinned
`pyserial` package from official PyPI:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install --index-url https://pypi.org/simple -r tools/requirements.txt
python3 tools/note4c.py ports
```

Only dependency installation needs Internet access. The manager never uses a
network connection or sends shell commands to the device. On later sessions,
activate the same virtual environment; there is no need to reinstall packages.

## Check the device and synchronize time

Use the USB port printed by `ports`, for example:

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 status
python3 tools/note4c.py --port /dev/cu.usbmodem1101 time-sync
```

You may omit `--port` only when exactly one candidate USB port is connected.
The clock command copies your computer's current UTC time and its actual current
UTC offset, so check the computer clock and timezone first. Supported dates are
2024 through 2099. USB status reports the current time. The stopped screen updates its clock on the next redraw. During a run, the minute-boundary refresh updates the clock and elapsed-time snapshot together. UTC timestamps in history remain unchanged; the offset affects display
and the local 23:59 star reset, not stored UTC history. Firmware defaults to UTC+08:00 until you override it.

To sync UTC time but explicitly choose the local display offset:

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 time-sync --utc-offset +08:00
```

Stop the timer before changing clock or timezone settings. To change only the
display offset, without changing the UTC clock:

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 timezone --utc-offset +08:00
python3 tools/note4c.py --port /dev/cu.usbmodem1101 timezone --utc-offset -05:00
```

Offsets must use signed HH:MM and be between -12:00 and +14:00, inclusive.
Settings persist on the device. This is a **fixed offset**, not a timezone rule:
it does not automatically change for daylight saving time or travel. Run
`time-sync` again after such changes to use the Mac's new current offset, or set
`timezone` explicitly. Sync results and `status` show `utc_offset_minutes` and
its readable UTC offset. `status` also prints any data errors the device reports.
Do not ignore storage errors. `status` also includes `daily_stars` (null when unavailable), `stars_available`, and `stars_reset_local_time: "23:59"`. A clock/offset correction crossing a reset interval clears the cosmetic stars without altering bests/history. A record after 23:59 belongs to the next interval and is not cleared at midnight.

## Edit activities in Finder

1. Stop the timer and open **NOTE4C** under Locations in Finder.
2. Open `activities.txt` in a plain-text editor. Write one activity per line.
3. Save as UTF-8 plain text and eject **NOTE4C** in Finder before unplugging.

```text
Brush teeth
拉伸
閱讀
```

Use 1–32 distinct names, each up to 24 printable ASCII or basic Chinese
characters (U+4E00–U+9FFF). Empty lines are ignored. The line order controls
the timer’s activity order. Existing names keep their IDs when reordered.
A changed name starts a new category, and previous records remain in history.

The timer applies valid edits when the USB host releases the drive. If the file
is invalid or the timer is running, the timer keeps the previous list and reports
the error on screen and in USB status. Reconnect to get a fresh copy of the active
list.

## Add WAV sound effects

Create an `audio` folder beside `activities.txt` on the NOTE4C drive. Copy in
mono, 16-bit PCM WAV files at 16 kHz. Give each file a distinct slot from
`01` to `99`, for example `01-soft-ping.wav`, using printable ASCII names.
The 256 KiB drive is shared with the activity list and filesystem overhead.
Eject the drive in Finder to load the updated sounds. Unreadable or unsupported
files are skipped.

The device starts at `00 Off`, which needs no file. During a run, UP selects
the previous occupied slot and DOWN selects the next, wrapping through `00`.
The selected sound plays immediately when a run starts or you change the
selection, then repeats every three seconds while timing. Selecting `00`
silences playback immediately. The filename appears to the right of the
activity, with long names shortened to fit. Stopping ends the repeat and plays
the selected sound once. The selection carries into the next run. While
stopped, UP and DOWN select activities.

## Advanced JSON activity management

First export the current list:

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 actions get --output actions.json
```

Edit `actions.json` in a plain-text editor. Example:

```json
{
  "schema_version": 1,
  "actions": [
    {"id": "brush-teeth", "name": "Brush teeth"},
    {"id": "stretch", "name": "Stretch"}
  ]
}
```

Use 1 to 32 actions. Each ID must be unique and contain 1 to 32 lowercase letters,
digits, hyphens, or underscores. Keep IDs stable when renaming actions so their
history remains associated with the same action. Display names must be 1 to 24
printable ASCII or basic Chinese characters (U+4E00–U+9FFF), at most 72 UTF-8
bytes, and cannot be blank. Accents, combining marks, emoji, and other scripts
are not supported by the device's renderer. Do not add extra keys.

Send the edited file:

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 actions put actions.json
python3 tools/note4c.py --port /dev/cu.usbmodem1101 status
```

Invalid files are rejected before opening the device. An existing output file is
replaced only once a complete valid export has been received and written.

## Save history

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 history --output history.json
```

The export is a JSON object with `schema_version: 1` and a `records` array. Keep
regular copies somewhere safe. Export does not erase device history. There is no
history-erase command. The maximum accepted/exported JSON size is 8 MiB.

## Start and stop

```sh
python3 tools/note4c.py --port /dev/cu.usbmodem1101 start
python3 tools/note4c.py --port /dev/cu.usbmodem1101 stop
```

These operate the currently selected action; use the device interface to select
an action. The timer keeps the shortest completed run as the best time. There is
no maximum/longest-time mode.

## Troubleshooting

- No port: use a data cable, reconnect, and rerun `ports`.
- Multiple ports: explicitly choose the Note4C's port; unplug/replug to identify it.
- Busy port: close other serial terminals, IDE monitors, and manager instances.
- Slow export: use `--timeout 60` before the command (maximum 300 seconds).
- Timeout or partial write: the device may have acted even if its response was
  lost. Run `status` before retrying a mutating command. The manager intentionally
  does not retry automatically.
- Malformed response/storage error: save the error and stop making changes until
  the device's storage/state can be checked. Export any readable history first.

The fake-serial test suite runs without a connected device or `pyserial`:

```sh
python3 -m unittest discover -s tests -p test_usb_manager.py -v
```
