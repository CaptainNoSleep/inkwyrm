#!/usr/bin/env bash
#
# flash-and-verify.sh - Experimental XTeink X4 app-firmware flasher
#
# macOS-only helper for flashing an app-only firmware image to an XTeink X4
# (ESP32-C3 with stock 16 MB flash). It preserves the stock bootloader and
# writes the image at offset 0x10000 by default.
#
# EXPERIMENTAL: this script writes directly to device flash. It performs
# pre-flash checks, creates a local full-chip backup, and verifies the write,
# but it cannot guarantee firmware compatibility, successful recovery, or
# prevention of device damage, data loss, or warranty loss. Review the device,
# firmware hash, partition check, and backup path carefully before confirming.
#
# Requires: macOS, bash, perl, shasum, stat, and esptool.
# Restore guidance: docs/flashing.md

set -euo pipefail
umask 077

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESTORE_DOC="${SCRIPT_DIR%/tools}/docs/flashing.md"
EXPECTED_FLASH_SIZE=16777216

FIRMWARE_IMAGE="${FIRMWARE_IMAGE:-}"
EXPECTED_SHA256="${EXPECTED_SHA256:-}"
TARGET_MAC="${TARGET_MAC:-}"
SERIAL_PORT="${SERIAL_PORT:-}"
FLASH_OFFSET="${FLASH_OFFSET:-0x10000}"
BAUD="${BAUD:-460800}"
BACKUP_DIR="${BACKUP_DIR:-./x4-backups}"
ESPTOOL="${ESPTOOL:-esptool}"
ESPTOOL_TIMEOUT="${ESPTOOL_TIMEOUT:-90}"

usage() {
  cat <<'EOF'
Usage:
  tools/flash-and-verify.sh --image PATH --sha256 SHA256 (--mac MAC | --port PORT) [options]

Required:
  --image PATH          Firmware image supplied by the release.
                        Environment: FIRMWARE_IMAGE
  --sha256 SHA256       Expected SHA-256 from that release.
                        Environment: EXPECTED_SHA256
  --mac MAC             Target device MAC address, for example AA:BB:CC:DD:EE:FF.
                        The script probes /dev/cu.usbmodem* ports and matches it.
                        Environment: TARGET_MAC
  --port PORT           Less-safe explicit serial port, for example
                        /dev/cu.usbmodem12345. The script reads its MAC and
                        requires that MAC as typed confirmation before flashing.
                        Environment: SERIAL_PORT

Optional:
  --offset OFFSET       App partition offset. Default: 0x10000.
                        Environment: FLASH_OFFSET
  --baud BAUD           Serial baud rate. Default: 460800.
                        Environment: BAUD
  --backup-dir DIR      Destination for full-chip backups. Default: ./x4-backups.
                        Environment: BACKUP_DIR
  --esptool COMMAND     esptool executable. Default: esptool.
                        Environment: ESPTOOL
  --timeout SECONDS     Per-esptool-command timeout. Default: 90.
                        Environment: ESPTOOL_TIMEOUT
  --restore-help        Print the restore documentation location and exit.
  -h, --help            Show this help.

Examples:
  tools/flash-and-verify.sh \
    --image ./firmware.bin \
    --sha256 <release-sha256> \
    --mac AA:BB:CC:DD:EE:FF

  tools/flash-and-verify.sh \
    --image ./firmware.bin \
    --sha256 <release-sha256> \
    --port /dev/cu.usbmodem12345
EOF
}

restore_guidance() {
  printf '\nRestore/rollback guidance is documented separately:\n  %s\n' "$RESTORE_DOC" >&2
  printf 'Do not perform a full-chip restore unless you understand its warnings.\n' >&2
}

die() {
  printf '\nERROR: %s\n' "$*" >&2
  restore_guidance
  exit 1
}

run_esptool() {
  printf '+ %q' "$ESPTOOL"
  printf ' %q' "$@"
  printf '\n'

  perl -e '
    my $timeout = shift @ARGV;
    $SIG{ALRM} = sub {
      print STDERR "esptool timed out after ${timeout} seconds\n";
      exit 124;
    };
    alarm $timeout;
    exec @ARGV or die "Could not execute esptool: $!\n";
  ' "$ESPTOOL_TIMEOUT" "$ESPTOOL" "$@"
}

normalize_mac() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr '-' ':'
}

extract_mac() {
  grep -Eio '([0-9a-f]{2}:){5}[0-9a-f]{2}' | head -n 1 | tr '[:upper:]' '[:lower:]'
}

extract_flash_size() {
  perl -ne '
    if (/Detected flash size:\s*([0-9]+)\s*([KMG])B\b/i) {
      my ($size, $unit) = ($1, uc $2);
      my %factor = (K => 1024, M => 1024 * 1024, G => 1024 * 1024 * 1024);
      print $size * $factor{$unit};
      exit;
    }
  '
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image)
      [[ $# -ge 2 ]] || die "--image requires a value."
      FIRMWARE_IMAGE="$2"
      shift 2
      ;;
    --sha256)
      [[ $# -ge 2 ]] || die "--sha256 requires a value."
      EXPECTED_SHA256="$2"
      shift 2
      ;;
    --mac)
      [[ $# -ge 2 ]] || die "--mac requires a value."
      TARGET_MAC="$2"
      shift 2
      ;;
    --port)
      [[ $# -ge 2 ]] || die "--port requires a value."
      SERIAL_PORT="$2"
      shift 2
      ;;
    --offset)
      [[ $# -ge 2 ]] || die "--offset requires a value."
      FLASH_OFFSET="$2"
      shift 2
      ;;
    --baud)
      [[ $# -ge 2 ]] || die "--baud requires a value."
      BAUD="$2"
      shift 2
      ;;
    --backup-dir)
      [[ $# -ge 2 ]] || die "--backup-dir requires a value."
      BACKUP_DIR="$2"
      shift 2
      ;;
    --esptool)
      [[ $# -ge 2 ]] || die "--esptool requires a value."
      ESPTOOL="$2"
      shift 2
      ;;
    --timeout)
      [[ $# -ge 2 ]] || die "--timeout requires a value."
      ESPTOOL_TIMEOUT="$2"
      shift 2
      ;;
    --restore-help)
      restore_guidance
      exit 0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

[[ -n "$FIRMWARE_IMAGE" ]] || die "A firmware image is required."
[[ -n "$EXPECTED_SHA256" ]] || die "An expected SHA-256 is required."
[[ -n "$TARGET_MAC" || -n "$SERIAL_PORT" ]] || die "Provide either --mac or --port."
[[ -z "$TARGET_MAC" || -z "$SERIAL_PORT" ]] || die "Use --mac or --port, not both."

command -v "$ESPTOOL" >/dev/null 2>&1 || die "esptool was not found: $ESPTOOL"
command -v shasum >/dev/null 2>&1 || die "shasum is required."
command -v stat >/dev/null 2>&1 || die "macOS stat is required."
command -v perl >/dev/null 2>&1 || die "perl is required."

[[ "$EXPECTED_SHA256" =~ ^[0-9A-Fa-f]{64}$ ]] || die "Expected SHA-256 must be 64 hexadecimal characters."
EXPECTED_SHA256="$(printf '%s' "$EXPECTED_SHA256" | tr '[:upper:]' '[:lower:]')"

if [[ "$FLASH_OFFSET" =~ ^0[xX][0-9A-Fa-f]+$ ]]; then
  OFFSET_DEC=$((16#${FLASH_OFFSET:2}))
elif [[ "$FLASH_OFFSET" =~ ^[0-9]+$ ]]; then
  OFFSET_DEC=$((10#$FLASH_OFFSET))
else
  die "Offset must be plain decimal or 0x-prefixed hexadecimal."
fi
OFFSET_HEX="$(printf '0x%X' "$OFFSET_DEC")"

[[ "$BAUD" =~ ^[1-9][0-9]*$ ]] || die "Baud must be a positive integer."
[[ "$ESPTOOL_TIMEOUT" =~ ^[1-9][0-9]*$ ]] || die "Timeout must be a positive integer."

# Gate 1: verify image existence, size, and release-provided checksum.
[[ -f "$FIRMWARE_IMAGE" ]] || die "Firmware image does not exist or is not a regular file: $FIRMWARE_IMAGE"
FIRMWARE_SIZE="$(stat -f '%z' "$FIRMWARE_IMAGE")"
[[ "$FIRMWARE_SIZE" -gt 0 ]] || die "Firmware image is empty: $FIRMWARE_IMAGE"

ACTUAL_SHA256="$(shasum -a 256 "$FIRMWARE_IMAGE" | awk '{print tolower($1)}')"
[[ "$ACTUAL_SHA256" == "$EXPECTED_SHA256" ]] || die \
  "Firmware SHA-256 does not match the release value. Expected $EXPECTED_SHA256, got $ACTUAL_SHA256."

printf 'Firmware check passed:\n  File: %s\n  Bytes: %s\n  SHA-256: %s\n' \
  "$FIRMWARE_IMAGE" "$FIRMWARE_SIZE" "$ACTUAL_SHA256"

# Gate 2: locate exactly the requested device. Never select an arbitrary port.
if [[ -n "$TARGET_MAC" ]]; then
  TARGET_MAC="$(normalize_mac "$TARGET_MAC")"
  [[ "$TARGET_MAC" =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]] || die "Invalid MAC address: $TARGET_MAC"

  shopt -s nullglob
  candidates=(/dev/cu.usbmodem*)
  shopt -u nullglob
  [[ ${#candidates[@]} -gt 0 ]] || die "No /dev/cu.usbmodem* ports found."

  SELECTED_PORT=""
  for candidate in "${candidates[@]}"; do
    probe_output=""
    if probe_output="$(run_esptool --port "$candidate" --baud "$BAUD" read-mac 2>&1)"; then
      candidate_mac="$(printf '%s\n' "$probe_output" | extract_mac || true)"
      if [[ "$candidate_mac" == "$TARGET_MAC" ]]; then
        SELECTED_PORT="$candidate"
        READ_MAC="$candidate_mac"
        break
      fi
    fi
  done

  [[ -n "$SELECTED_PORT" ]] || die \
    "Could not find a responding /dev/cu.usbmodem* device with MAC $TARGET_MAC."
  PORT_MODE=false
else
  SELECTED_PORT="$SERIAL_PORT"
  [[ -c "$SELECTED_PORT" ]] || die "Specified port is not a character device: $SELECTED_PORT"

  mac_output=""
  if ! mac_output="$(run_esptool --port "$SELECTED_PORT" --baud "$BAUD" read-mac 2>&1)"; then
    die "Could not read the MAC address from $SELECTED_PORT."
  fi
  READ_MAC="$(printf '%s\n' "$mac_output" | extract_mac || true)"
  [[ -n "$READ_MAC" ]] || die "esptool did not return a recognizable MAC address for $SELECTED_PORT."
  PORT_MODE=true
fi

printf '\nSelected device:\n  Port: %s\n  MAC:  %s\n' "$SELECTED_PORT" "$READ_MAC"

# Gate 3: require the expected 16 MB flash size before any backup or write.
flash_id_output=""
if ! flash_id_output="$(run_esptool --port "$SELECTED_PORT" --baud "$BAUD" flash-id 2>&1)"; then
  die "Could not detect the flash size for $SELECTED_PORT."
fi

DETECTED_FLASH_SIZE="$(printf '%s\n' "$flash_id_output" | extract_flash_size || true)"
[[ -n "$DETECTED_FLASH_SIZE" ]] || die \
  "esptool did not return a recognizable detected flash size. Refusing to claim a full-chip backup."

[[ "$DETECTED_FLASH_SIZE" -eq "$EXPECTED_FLASH_SIZE" ]] || die \
  "Detected flash size is $DETECTED_FLASH_SIZE bytes, not the required 16 MB ($EXPECTED_FLASH_SIZE bytes). Refusing to continue."

printf 'Flash-size check passed:\n  Detected: %s bytes (16 MB)\n' "$DETECTED_FLASH_SIZE"

# Gate 4: inspect the on-device partition table before any flash write.
PARTITION_TABLE="$(mktemp -t x4-partition-table.XXXXXX)"
trap 'rm -f "$PARTITION_TABLE"' EXIT

if ! run_esptool --port "$SELECTED_PORT" --baud "$BAUD" read-flash 0x8000 0x1000 "$PARTITION_TABLE"; then
  die "Could not read the device partition table."
fi

if ! perl - "$PARTITION_TABLE" "$OFFSET_DEC" "$FIRMWARE_SIZE" <<'PERL'
use strict;
use warnings;

my ($path, $wanted_offset, $firmware_size) = @ARGV;
open my $fh, '<:raw', $path or die "Could not open partition table: $!\n";
read($fh, my $data, 0x1000) or die "Could not read partition table\n";

my $matched = 0;
for (my $position = 0; $position + 32 <= length($data); $position += 32) {
    my ($magic, $type, $subtype, $offset, $size) =
        unpack('vCCVV', substr($data, $position, 12));

    next unless $magic == 0x50AA;
    next unless $type == 0x00; # ESP-IDF application partition type

    if ($offset == $wanted_offset) {
        $matched = 1;
        if ($size >= $firmware_size) {
            printf "Partition check passed: app partition at 0x%X, size %u bytes\n",
                $offset, $size;
            exit 0;
        }

        printf STDERR
            "App partition at 0x%X is only %u bytes; firmware is %u bytes\n",
            $offset, $size, $firmware_size;
        exit 2;
    }
}

if (!$matched) {
    printf STDERR "No app-type partition exists at requested offset 0x%X\n", $wanted_offset;
}
exit 3;
PERL
then
  die "Partition-table safety check failed. Refusing to flash."
fi

# Gate 5: create and validate a complete 16 MB local backup plus manifest.
mkdir -p "$BACKUP_DIR" || die "Could not create backup directory: $BACKUP_DIR"
[[ -d "$BACKUP_DIR" && -w "$BACKUP_DIR" ]] || die "Backup directory is not writable: $BACKUP_DIR"

TIMESTAMP="$(date -u '+%Y%m%dT%H%M%SZ')"
SAFE_MAC="${READ_MAC//:/-}"
BACKUP_FILE="${BACKUP_DIR%/}/x4-${SAFE_MAC}-${TIMESTAMP}-full-16mb.bin"
MANIFEST_FILE="${BACKUP_FILE}.manifest"

[[ ! -e "$BACKUP_FILE" && ! -e "$MANIFEST_FILE" ]] || die "Backup filename already exists: $BACKUP_FILE"

if ! run_esptool --port "$SELECTED_PORT" --baud "$BAUD" read-flash 0x0 0x1000000 "$BACKUP_FILE"; then
  die "Full-chip backup failed. No flash write was attempted."
fi

BACKUP_SIZE="$(stat -f '%z' "$BACKUP_FILE")"
[[ "$BACKUP_SIZE" -eq "$EXPECTED_FLASH_SIZE" ]] || die \
  "Backup is not exactly 16 MB ($BACKUP_SIZE bytes). No flash write was attempted."

BACKUP_SHA256="$(shasum -a 256 "$BACKUP_FILE" | awk '{print tolower($1)}')"
printf 'filename=%s\nbytes=%s\nsha256=%s\ntimestamp_utc=%s\n' \
  "$(basename "$BACKUP_FILE")" "$BACKUP_SIZE" "$BACKUP_SHA256" "$TIMESTAMP" >"$MANIFEST_FILE"

printf '\nBackup check passed:\n  Backup:   %s\n  Manifest: %s\n  SHA-256:  %s\n' \
  "$BACKUP_FILE" "$MANIFEST_FILE" "$BACKUP_SHA256"

# Gate 6: require an intentional, typed confirmation after all checks succeed.
printf '\nFLASH SUMMARY\n'
printf '  Port:       %s\n' "$SELECTED_PORT"
printf '  MAC:        %s\n' "$READ_MAC"
printf '  Firmware:   %s\n' "$FIRMWARE_IMAGE"
printf '  SHA-256:    %s\n' "$ACTUAL_SHA256"
printf '  Offset:     %s\n' "$OFFSET_HEX"
printf '  Backup:     %s\n' "$BACKUP_FILE"

if [[ "$PORT_MODE" == true ]]; then
  printf '\nThis uses the less-safe --port option. Type the displayed MAC address exactly to continue: '
  read -r confirmation
  confirmation="$(normalize_mac "$confirmation")"

  [[ "$confirmation" == "$READ_MAC" ]] || {
    printf '\nAborted. No flash write was performed.\n'
    exit 1
  }
else
  printf '\nThis will write firmware to device flash. Type FLASH to continue: '
  read -r confirmation

  [[ "$confirmation" == "FLASH" ]] || {
    printf '\nAborted. No flash write was performed.\n'
    exit 1
  }
fi

# Gate 7: write only the requested app image, then verify it byte-for-byte.
if ! run_esptool --port "$SELECTED_PORT" --baud "$BAUD" \
  write-flash "$OFFSET_HEX" "$FIRMWARE_IMAGE"; then
  die "Flash write failed. The verified full-chip backup remains at: $BACKUP_FILE"
fi

if ! run_esptool --port "$SELECTED_PORT" --baud "$BAUD" \
  verify-flash "$OFFSET_HEX" "$FIRMWARE_IMAGE"; then
  die "Flash verification failed. Do not assume the device is usable. Backup: $BACKUP_FILE"
fi

printf '\nSuccess: firmware was written and verified at %s.\n' "$OFFSET_HEX"
printf 'Keep the backup and manifest in a safe location:\n  %s\n  %s\n' "$BACKUP_FILE" "$MANIFEST_FILE"
