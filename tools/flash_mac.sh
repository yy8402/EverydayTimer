#!/bin/bash
# Deliberately limited to verified, prebuilt distribution binaries. No build/download.
set -euo pipefail
if [[ $# -ne 2 || ( "$2" != "--install" && "$2" != "--update" ) || -z "$1" ]]; then
  echo "Usage: $0 /dev/cu.usbmodemNN --install|--update" >&2
  exit 2
fi
PORT="$1"
MODE="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
python3 - <<'PY'
import esptool
if esptool.__version__ != '4.9.0':
    raise SystemExit('Expected esptool 4.9.0. Install tools/requirements.txt in your virtual environment.')
PY
# mktemp creates a unique directory and refuses to overwrite any existing backup.
mkdir -p "$ROOT/backups"
BACKUP_DIR="$(mktemp -d "$ROOT/backups/note4c-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
BACKUP="$BACKUP_DIR/full-flash-16MiB.bin"
# Stage and hash-check all files before touching the device. Keep them with backup.
python3 - "$DIST" "$BACKUP_DIR" <<'PY'
import hashlib
from pathlib import Path
import re
import shutil
import sys
source, target = map(Path, sys.argv[1:])
names = ('bootloader.bin', 'partition-table.bin', 'note4c_timer.bin')
manifest = (source / 'SHA256SUMS').read_text(encoding='ascii')
expected = {}
for line in manifest.splitlines():
    if not line.strip():
        continue
    match = re.fullmatch(r'([0-9a-fA-F]{64}) [ *](.+)', line)
    if not match:
        raise SystemExit('Malformed SHA256SUMS entry.')
    digest, name = match.groups()
    if name in expected:
        raise SystemExit('Duplicate SHA256SUMS entry: ' + name)
    expected[name] = digest.lower()
for name, limit in zip(names, (0x8000, 0x8000, 0x2B0000)):
    if name not in expected:
        raise SystemExit('Missing checksum: ' + name)
    data = (source / name).read_bytes()
    if not 0 < len(data) <= limit:
        raise SystemExit('Invalid binary size: ' + name)
    if hashlib.sha256(data).hexdigest() != expected[name]:
        raise SystemExit('Checksum mismatch: ' + name)
    (target / name).write_bytes(data)
(target / 'SHA256SUMS').write_text(manifest, encoding='ascii')
print('Distribution binaries verified.')
PY
printf 'Reading full 16 MiB backup before any write. Backup folder:\n%s\n' "$BACKUP_DIR"
python3 -m esptool --chip esp32s3 --port "$PORT" --baud 460800 --before default_reset --after no_reset read_flash 0 0x1000000 "$BACKUP"
python3 - "$BACKUP" <<'PY'
import hashlib
from pathlib import Path
import sys
path = Path(sys.argv[1])
if path.stat().st_size != 0x1000000:
    raise SystemExit('Incomplete backup: expected exactly 16 MiB. Flashing refused.')
digest = hashlib.sha256(path.read_bytes()).hexdigest()
checksum = path.with_suffix(path.suffix + '.sha256')
with checksum.open('x') as out:
    out.write(digest + '  ' + path.name + '\n')
if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
    raise SystemExit('Backup verification failed. Flashing refused.')
print('Backup SHA-256: ' + digest)
print('Backup size and local checksum verified. Keep this folder safe.')
PY
if [[ "$MODE" == "--install" ]]; then
  echo 'INITIAL INSTALL: replaces the bootloader, partition table and app, and ERASES timer storage (0x300000..0xFFFFFF).'
  echo 'Existing foreign firmware/data are not preserved in place; recovery requires your full backup.'
else
  echo 'UPDATE: writes bootloader, partition table and app only; does not erase timer storage.'
  echo 'Use update only for this timer firmware with the the same timer storage partition. Foreign firmware/data may be incompatible.'
fi
echo 'Keep USB connected and prevent computer sleep until verification finishes.'
printf 'Type FLASH to proceed, or anything else to cancel: '
IFS= read -r CONFIRM || CONFIRM=''
if [[ "$CONFIRM" != "FLASH" ]]; then
  printf 'Cancelled. Backup retained at %s\n' "$BACKUP"
  echo 'Device remains in download mode. Press reset or power-cycle it to return to the previous firmware.'
  exit 0
fi
if [[ "$MODE" == "--install" ]]; then
  python3 -m esptool --chip esp32s3 --port "$PORT" --baud 460800 --before default_reset --after no_reset erase_region 0x300000 0xd00000
fi
python3 -m esptool --chip esp32s3 --port "$PORT" --baud 460800 --before default_reset --after no_reset write_flash \
  0x0 "$BACKUP_DIR/bootloader.bin" \
  0x8000 "$BACKUP_DIR/partition-table.bin" \
  0x10000 "$BACKUP_DIR/note4c_timer.bin"
python3 -m esptool --chip esp32s3 --port "$PORT" --baud 460800 --before default_reset --after hard_reset verify_flash \
  0x0 "$BACKUP_DIR/bootloader.bin" \
  0x8000 "$BACKUP_DIR/partition-table.bin" \
  0x10000 "$BACKUP_DIR/note4c_timer.bin"
printf 'Firmware verified. Full backup retained at:\n%s\n' "$BACKUP"
echo 'Reconnect USB, run status, and synchronize the clock before timing.'
