# Install or update from a Mac

Generate the images in `dist` using the [build instructions](BUILD.md) for the exact supported board. Keep your Mac awake,
connect directly with a data-capable USB cable, and close serial terminals. Initial
installation replaces the existing firmware and storage, not just an application.
A full backup is required even for an update.

## Preparation

From this project directory:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install --index-url https://pypi.org/simple -r tools/requirements.txt
python3 tools/note4c.py ports
```

Dependency installation needs Internet access. The flash helper does not build or
download firmware. It requires `dist/bootloader.bin`, `dist/partition-table.bin`,
`dist/note4c_timer.bin`, and their SHA-256 entries in `dist/SHA256SUMS`.

## First installation

```sh
bash tools/flash_mac.sh /dev/cu.usbmodem1101 --install
```

The helper verifies distribution hashes, reads all 16 MiB of flash into a unique
`backups/note4c-TIMESTAMP-XXXXXX/full-flash-16MiB.bin`, checks its exact size and
local SHA-256, and keeps a checksum beside it. It then requires you to type `FLASH`.
Backup, erase, and write commands explicitly use `--after no_reset` so no
application boots between steps. Only successful final verification uses
`--after hard_reset`. Each connection uses `--before default_reset` to enter
the download loader.
Cancellation retains your backup and leaves the device in download mode. Press
reset or power-cycle the device to return to the previous firmware. Copy the complete backup folder somewhere safe.
A matching local checksum verifies the saved file, not that the original firmware
was bootable or that flash encryption/security settings permit restoration.

Initial installation erases `0x300000` through `0xFFFFFF` (13 MiB) to blank timer
storage, then writes only these three binaries:

- `bootloader.bin` at `0x0`
- `partition-table.bin` at `0x8000`
- `note4c_timer.bin` at `0x10000`

It does not promise to preserve a different manufacturer's/custom firmware or
its data in place. Full restoration uses the complete backup, not these binaries.

## Subsequent timer updates

Export actions and history using [USB.md](USB.md). On firmware 1.6.0 and
later, eject the NOTE4C drive, then enter the ROM download mode with the
BOOT and RESET buttons as described below. Use the serial port that appears
in download mode.

```sh
bash tools/flash_mac.sh /dev/cu.usbmodem1101 --update
```

Use `--update` for timer firmware with storage at `0x300000`–`0xFFFFFF`.
The helper makes a fresh full backup, asks for `FLASH`, and writes and verifies
the three binaries. It preserves the timer storage partition. Version 1.6.0
allocates a separate 256 KiB activity drive from the previous app partition’s
unused space.

After either operation, reconnect USB and run `status` followed by `time-sync` as
described in [USB.md](USB.md). Inspect any data errors before making changes.

## Restore a full backup

Restoring replaces everything on the device, including all newer actions/history.
Export those first if readable. Use the exact 16 MiB backup from this device and
verify its saved checksum. Substitute your real backup folder and USB port:

```sh
cd backups/note4c-YOUR-BACKUP-FOLDER
shasum -a 256 -c full-flash-16MiB.bin.sha256
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 --before default_reset --after no_reset write_flash 0x0 full-flash-16MiB.bin
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 460800 --before default_reset --after hard_reset verify_flash 0x0 full-flash-16MiB.bin
```

Only run the write after the checksum reports `OK`. The write leaves the
device in download mode; verification completes before the final reset/boot. Restore is a destructive
manual operation and does not ask for the helper's `FLASH` confirmation. Never
restore another device's flash image. Security fuses, flash encryption, and secure
boot can prevent ordinary backup/restore; this workflow does not bypass them.
If esptool refuses access, stop rather than forcing writes or changing fuses.

## If automatic USB boot entry fails

Use a USB **data** cable and close other serial monitors. With USB connected, hold the front **BOOT** button, briefly press the recessed **RESET** button on the left, then release BOOT. Re-list the Mac ports and run the helper using the port currently present. After a successful install or cancellation in download mode, briefly press RESET or power-cycle to run firmware. Do not hold BOOT during normal timer use at reset, because GPIO0 selects the bootloader.

This recovery sequence is documented by the [official NOTE4C reference page](https://www.zectrix.com/en/developer/projects/note4c-reference.html). If the connection still fails, do not try a different model's firmware.
