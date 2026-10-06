#!/bin/sh
# Puts a saved release back on the Tab5 without rebuilding anything.
#
#   tools/restore_release.sh v1.0.0            # firmware only: settings on the device are kept
#   tools/restore_release.sh v1.0.0 --full     # the whole 16MB flash dump: settings as of the backup
#   tools/restore_release.sh v1.0.0 --port /dev/cu.usbmodemXXXX
#
# Releases live in firmware_backup/<version>/ (bootloader.bin, partitions.bin, firmware.bin,
# written at the offsets PlatformIO uses for the ESP32-P4) and, for --full, in
# firmware_backup/tab5_<version>_full_16MB.bin. See docs/development-setup.md.
set -eu

cd "$(dirname "$0")/.."

version=""
full=0
port=""
while [ $# -gt 0 ]; do
  case "$1" in
    --full) full=1 ;;
    --port) shift; port="$1" ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) version="$1" ;;
  esac
  shift
done
if [ -z "$version" ]; then
  echo "usage: $0 <version> [--full] [--port /dev/cu.usbmodemXXXX]" >&2
  echo "saved releases:" >&2
  ls -d firmware_backup/v* 2>/dev/null | sed 's|firmware_backup/|  |' >&2
  exit 2
fi

if [ -z "$port" ]; then
  # The device name changes with the USB port in use; esptool's own autodetection also
  # finds other serial devices, so insist on exactly one Tab5.
  set -- /dev/cu.usbmodem*
  if [ $# -ne 1 ] || [ ! -e "$1" ]; then
    echo "expected exactly one /dev/cu.usbmodem* device, found: $*; pass --port" >&2
    exit 1
  fi
  port="$1"
fi

if [ $full -eq 1 ]; then
  image="firmware_backup/tab5_${version}_full_16MB.bin"
  [ -f "$image" ] || { echo "no full dump at $image" >&2; exit 1; }
  echo "Writing the full 16MB image $image (this takes a few minutes)..."
  exec uvx esptool --port "$port" --baud 921600 write-flash 0x0 "$image"
fi

dir="firmware_backup/$version"
for f in bootloader.bin partitions.bin firmware.bin; do
  [ -f "$dir/$f" ] || { echo "missing $dir/$f" >&2; exit 1; }
done
(cd "$dir" && shasum -a 256 -c SHA256SUMS --quiet) || { echo "checksum mismatch in $dir" >&2; exit 1; }
echo "Writing $version from $dir (settings on the device are kept)..."
exec uvx esptool --port "$port" --baud 921600 write-flash \
  --flash-mode dio --flash-freq 40m --flash-size 16MB \
  0x2000 "$dir/bootloader.bin" \
  0x8000 "$dir/partitions.bin" \
  0x10000 "$dir/firmware.bin"
