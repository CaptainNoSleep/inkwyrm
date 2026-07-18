# Changelog

## InkWyrm 0.26: dark mode + WiFi that actually joins (STAGED)

- **Dark mode**: double-tap the power button while reading to flip white-on-black
  and back (single-tap stays unused; hold still sleeps). V1 applies to pages
  rendered without text smoothing or images; the smoothed/grayscale pipeline
  stays light for now. Any light/dark transition gets a full refresh so the
  panel never ghosts.
- **WiFi join hardening**, built from upstream diagnosis + community research on
  this exact chip: modem power-save disabled during joins (fixes silent
  handshake stalls), join timeout 15s to 25s, one silent retry at reduced radio
  power (the ESP32-C3 brownout cure), full power restored on every fresh
  attempt, immediate failure detection.


## InkWyrm 0.25.2 (flashed 2026-07-18)

- The auto-sync toggles now live on the device's KOReader Sync settings screen
  (they were previously reachable only through the web settings API). Rows show
  Enabled/Disabled and flip on confirm; Authenticate moved to the last row.


## InkWyrm 0.25.1: automatic position sync (flashed 2026-07-18)

- Auto-pull on book open: the reader silently fetches the newest position from the
  sync server and jumps there when it is ahead of the local spot. Auto-push on
  close sends your position back. Both silent, both fail-safe (any problem keeps
  the local position; nothing ever pops up mid-read). Two toggles under Settings,
  KOReader Sync section.
- Review hardening: a persisted one-shot guard breaks any possible sync-restart
  loop, and auto-pull refuses to run if that guard cannot be saved.
- SD config import now carries sync server credentials ([sync] section), so a
  card-drop config file sets up WiFi and sync with zero on-device typing.

## InkWyrm 0.25, the 1.4.1 rebase (flashed and device-confirmed 2026-07-18)

First InkWyrm release on the CrossPoint 1.4.1 base. Inherits upstream's memory work
(#2106 tiled grayscale plus the 1.4.0 OOM fixes) and speed improvements natively; the
0.18 to 0.24 heap-chasing patches from the old 1.3.0.27 base are expected obsolete
(device test pending). Features re-applied so far, each build-verified and reviewed:

- **Reading-stats core**: session tracking plus `reading_stats.json` (formatVersion 6,
  byte-compatible with KOReader-style stats tooling). End-of-book completion mark restored;
  deep-sleep save added. No stats UI yet.
- **New v2 InkWyrm logo** (dragon-around-book, 2026-07-18 art) on boot (white-on-black
  lockup with version) and sleep (mark); generated via `scripts/generate_logo_header.py`.
  Version string `1.4.1+inkwyrm-0.25-dev` on the Settings screen.
- **Compact Info status bar**: clock, chapter title, reading-pace time-left estimate,
  progress bar with chapter ticks; overlap-safe slot layout.
- **Highlighting and annotation**: cursor-anywhere selection, per-book sync-ready
  highlight store (formatVersion 1), highlights list with jump-to-highlight, underline
  overlay. Post-review hardening: strict-weak-ordering sort fix (latent std::sort UB
  inherited from the old base), page-turn early-exit (no extraction cost unless the
  page has a highlight), variant access via std::get_if.
- **SD config import** (`/inkwyrm.conf`) with all 0.16 boot-watchdog RCA mitigations;
  font tokens mapped to the 1.4.1 font set.
- **KOReaderSync WiFi heap gap: closed upstream.** 1.4.1 already silent-restarts back
  to the reader after sync; verified in-code, nothing ported.

Also in 0.25: cache-time typography polish (CSS line-height, small caps, drop caps;
layout caches auto-rebuild), Pick-Random on the home menu, and a home-screen
sync-freshness indicator.

Deferred past 0.25 (with design notes in the repo): presets/live preview (needs a
legacy font-value adapter and a new preview screen), on-device stats screens,
Reading Guide (needs the favorites subsystem), SD-font offload (flash at 81.2%,
ceiling 85%).

---

Earlier releases (0.1 to 0.24) shipped from the 1.3.0.27 base; their changelog lives on
the `master` branch until the rebase supersedes it.
