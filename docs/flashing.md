# Flashing firmware on an XTeink X4

`tools/flash-and-verify.sh` is an experimental macOS helper for installing an app-only firmware image on an XTeink X4 with an ESP32-C3 and stock 16 MB flash.

It reuses the device's existing bootloader and writes the app image at offset `0x10000`. Before writing, it checks the release hash, identifies the requested device, checks the partition table, creates a full-chip local backup, and requires typed confirmation. It then verifies the written image.

This is at your own risk. These checks reduce avoidable mistakes, but cannot guarantee firmware compatibility, successful recovery, prevention of device damage, data loss, or warranty loss. Review the selected device, release hash, and backup path before typing `FLASH`.

## Requirements

- macOS. This tool is intentionally macOS-oriented and uses `/dev/cu.*`, `stat -f`, and `shasum`.
- An XTeink X4 with ESP32-C3 and stock 16 MB flash.
- A USB data cable.
- `esptool`.
- Standard macOS command-line tools, including `bash`, `perl`, `shasum`, and `stat`.

Install `esptool` with Python's package installer:

```bash
python3 -m pip install --user esptool
```

If the `esptool` command is not on your shell path afterward, invoke the script with `--esptool` and the full path to that executable.

## Get the firmware and SHA-256

Download the firmware image from an InkWyrm release. Obtain the SHA-256 value from the same release page or its accompanying checksum file.

Do not use a hash copied from an unrelated release, issue, chat message, or build log. The script refuses to flash when the image's computed SHA-256 differs from the value you provide.

## Find your device MAC

Connect the XTeink X4 by USB, then use `esptool` against its serial port:

```bash
esptool --port /dev/cu.usbmodemYOURDEVICE read-mac
```

The X4 normally appears as a `/dev/cu.usbmodem*` device on macOS. You can list candidates in Finder-independent form with:

```bash
ls /dev/cu.usbmodem*
```

Use the MAC printed by `read-mac` with `--mac`. This is the safest normal workflow because the script probes candidate ports and only selects the device whose MAC matches your value.

## Flash an image

From the repository root:

```bash
tools/flash-and-verify.sh \
  --image ./path/to/firmware.bin \
  --sha256 YOUR_RELEASE_SHA256 \
  --mac AA:BB:CC:DD:EE:FF
```

Alternatively, specify a known serial port:

```bash
tools/flash-and-verify.sh \
  --image ./path/to/firmware.bin \
  --sha256 YOUR_RELEASE_SHA256 \
  --port /dev/cu.usbmodemYOURDEVICE
```

When `--port` is used, the script still reads and displays the device MAC. It never silently chooses the first connected USB serial device.

Optional settings:

- `--offset OFFSET` changes the app offset. The default is `0x10000`.
- `--baud BAUD` changes the serial speed. The default is `460800`.
- `--backup-dir DIR` changes where backups are written. The default is `./x4-backups`.

The script refuses to continue unless the connected device has an app-type partition at the requested offset that is large enough for the firmware image.

## Backups

Before any flash write, the script reads the entire 16 MB chip into a timestamped file in `./x4-backups` by default. It verifies that the backup is exactly 16 MB and writes a neighboring manifest containing:

- Backup filename
- Byte count
- SHA-256
- UTC timestamp

Keep both files somewhere safe before experimenting further. A backup is useful recovery material, but it is not a guarantee that recovery will succeed.

## Restore / rollback

A full-chip restore overwrites the entire device flash, including its bootloader, partition table, application data, and any other stored content. It is not a routine troubleshooting command.

Only restore a backup when you understand that it will replace everything currently on the device, you have confirmed that the backup came from that exact device, and you have considered whether newer data must be preserved first.

After selecting the correct serial port, the restore command is:

```bash
esptool --port /dev/cu.usbmodemYOURDEVICE --baud 460800 \
  write-flash 0x0 ./x4-backups/your-full-16mb-backup.bin
```

Then verify the restored backup:

```bash
esptool --port /dev/cu.usbmodemYOURDEVICE --baud 460800 \
  verify-flash 0x0 ./x4-backups/your-full-16mb-backup.bin
```

Do not substitute a backup from another device. Do not run a full-chip restore casually after a failed flash; first confirm the selected port, the backup filename, its manifest, and its SHA-256.

## Known limitations

- Supports macOS only; it is not presented as portable to Linux or Windows.
- Assumes an XTeink X4 with ESP32-C3 and stock 16 MB flash.
- Assumes the existing bootloader and partition table are appropriate for the app image.
- Does not detect or refuse secure-boot configurations.
- Does not detect or refuse flash-encryption configurations.
- Cannot guarantee that a device will boot after flashing or that a backup restore will repair every failure mode.
