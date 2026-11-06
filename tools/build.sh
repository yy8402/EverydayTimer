#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if ! command -v idf.py >/dev/null; then
  echo 'Activate ESP-IDF v5.5.1 first: source /path/to/esp-idf/export.sh' >&2
  exit 1
fi
if [[ "$(idf.py --version)" != 'ESP-IDF v5.5.1' ]]; then
  echo 'This release is pinned to ESP-IDF v5.5.1.' >&2
  exit 1
fi
idf.py -C "$ROOT" build
mkdir -p "$ROOT/dist"
cp "$ROOT/build/bootloader/bootloader.bin" "$ROOT/dist/bootloader.bin"
cp "$ROOT/build/partition_table/partition-table.bin" "$ROOT/dist/partition-table.bin"
cp "$ROOT/build/note4c_timer.bin" "$ROOT/dist/note4c_timer.bin"
(cd "$ROOT/dist" && shasum -a 256 bootloader.bin partition-table.bin note4c_timer.bin > SHA256SUMS)
printf 'Built and checksummed firmware in %s/dist\n' "$ROOT"
