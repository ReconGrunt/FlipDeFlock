# Changelog

## v0.21
Roadmap sprint (3-agent design + implement, see AGENT_SWARM.md):
- **NFC default-key audit:** on a MIFARE Classic, a new "Deep" check captures the
  UID and tries the Flipper's on-SD key dictionary against every sector, then
  reports how many open with **factory/default keys** (trivially cloneable) — the
  core "is this badge using default keys?" access-control question. Reads the
  stock dictionary (no bundled keys); UID + default-keyed count go into the
  report. (mfkey32 intentionally deferred — it requires active card emulation.)
- **On-device Flock map:** a live map that plots detected ALPR cameras by
  bearing/distance around your GPS position — auto-fit zoom, heading tick,
  confidence-by-dot-size, scale bar. Pure visualization, no new radio activity.
- **Share to DeFlock (phone handoff):** renders a QR per marked camera that opens
  DeFlock on your phone at that location, so you contribute through the official
  app's review flow. The Flipper/ESP **never touch a network** — passive-only
  stays literally true. (Direct OSM submission deferred: it needs OAuth2/TLS on
  the ESP, which would break the no-network promise.) QR via vendored Nayuki
  qrcodegen (MIT).

## v0.20
- **Anti-stalking precision model** (Tier-2): a BLE tracker is flagged
  "following" only when seen >=4 times over a >=90 s window at >=3 distinct
  observer waypoints spanning >=150 m — kills urban false positives (a
  stationary shop tag, a single drive-by) while a real follower still clears it
  easily. Thresholds are tunable `#define`s; the detail view shows the track.
- **CI:** non-failing API-87.1 drift warning on every build; a `release.yml`
  attaches the `.fap` to `v*` tag releases automatically.
- **Docs:** refreshed the GitHub description/topics and README roadmap.

## v0.19
- BLE WiGLE CSV export (Type=BLE): the BLE/Tracker scan now also writes a
  `ble_*.wigle.csv` (geotagged devices only) next to the WiFi one, sharing one
  WigleWifi-1.4 header. (Tier-2 "safe" item from the audit sprint.)

## v0.18
5-agent audit sprint (see AGENT_SWARM.md / SPRINT_SUMMARY.md):
- **Flasher:** fast-baud (921600) now actually works — it was calling the
  non-stub rate API after loading the stub, which always failed.
- **Reports:** GeoJSON now uses OSM/DeFlock tags (`man_made=surveillance`,
  `surveillance:type=ALPR`) so points import; CSV/WiGLE fields are RFC-4180
  escaped (a comma/quote in an SSID no longer corrupts rows); WiGLE omits
  no-GPS-fix rows (no "Null Island"); partial report-write failures now surface.
- **Detection:** deauth alert needs a real flood (>=5/interval), not a single
  frame; added Flock's own registered OUI `B4:1E:52`; ISO15693 graded WEAK
  (UID-only/cloneable).
- **BLE:** detect Google Find My Device trackers (`0xFEAA`:
  Pebblebee/Chipolo/Motorola/Eufy). Geotag hysteresis removes tag jitter.
- **Robustness:** settings baud clamped to valid values; Marauder scraper has a
  bigger line buffer + bounded SSID extraction.

## v0.17
- Clearer support for **not flashing** (keeping Marauder). Renamed the setting to
  "Board Mode" (Marauder / Companion). In Marauder mode the companion-only
  screens (WiFi Audit, BLE/Tracker Scan) now explain they need the companion
  firmware instead of showing a dead screen, and About shows the active mode and
  what each one does.

## v0.16
- Fix: the ESP board kept scanning after you exited the app. The stop command
  was being cut off because the UART was torn down before it finished
  transmitting — now drained first. Works on Marauder and the companion (ships
  in the .fap, no re-flash). The companion firmware also fully idles on stop
  (leaves dual-band mode, parks channel hopping/status).

## v0.15
- Flasher correctness pass (code audit). Backup now reads the final flash chunk
  (off-by-one in the library's read/verify bounds); flashing pads images to
  4-byte alignment so arbitrary .bin sizes work; the write is MD5-**verified**
  and failures are reported (no more "done" on a bad flash); UART is drained
  before the fast-baud switch (prevents desync); robust partial-read handling;
  a partial backup is deleted on abort. Plus minor UI/throughput tweaks.

## v0.14
- Fix "not enough RAM to run app": the flasher bundled flash stubs for ~10 ESP
  chips (a FAP loads fully into RAM). Trimmed to ESP32 only, cutting the .fap
  from ~182 KB back to ~100 KB. (Flasher now supports classic ESP32 boards;
  other chips can be re-added if RAM allows.)

## v0.13
- Flasher "Flash Speed" setting: Safe (115200) or Fast (921600). Fast raises the
  link after connect for much quicker backup/flash; falls back to Safe on failure.

## v0.12
- In-app ESP32 flasher: **back up** the board's current firmware to SD and
  **flash a .bin** (companion / Marauder / a backup) straight from the Flipper -
  bounce between Marauder and the FlipDeFlock companion, no computer. Built on
  Espressif's esp-serial-flasher. Manual bootloader entry (hold BOOT, tap RESET).

## v0.11
- Device tagging: mark/untag any WiFi AP or BLE device (Tag button in detail);
  tagged items show `*` in the list and are flagged in the saved reports.

## v0.10
- Dual-band cadence tuned WiFi-biased (9 s WiFi / 3 s BLE).
- GPS is now OFF by default (existing installs keep their saved choice).
- Settings persist across reboots (saved on every change).

## v0.9
- Dual-band Flock detection: the companion firmware now interleaves WiFi (2.4GHz
  promiscuous) with periodic BLE scans, so Flock/Raven is detected over both
  radios; BLE-Flock hits merge into the Flock list and reports.
- Broader BLE Flock signatures (Penguin/FS Ext Battery names, Raven service
  UUIDs, Flock OUIs) beyond the mfg-id check. BLE kept resident (no heap churn).

## v0.8
- Stronger rogue/evil-twin heuristic: same SSID on multiple BSSIDs with
  mismatched security is flagged as a likely evil twin.
- Catalog-readiness: changelog, funding info, submission docs.

## v0.7
- BLE / Tracker Scan: detect Flock/Raven BLE beacons and AirTag/Tile/SmartTag
  trackers; flag a tracker that follows you across GPS waypoints (anti-stalking).

## v0.6
- Extra Flock heuristics (wildcard probe + addr1 silent receiver).
- Capture observer heading (GPS course) in the GeoJSON.
- OUI vendor lookup in the WiFi audit; KML export for Flock reports.

## v0.5
- Deauth attribution (names the attacked BSSID + channel).
- WiGLE CSV export. CI-built ESP32 firmware .bin for the Flipper ESP Flasher app.

## v0.4
- Deauth/disassoc flood detection with a live alert; evil-twin (duplicate SSID).

## v0.3
- WiFi security audit (auth/cipher/WPS grading) via the companion firmware.

## v0.2
- App icon; real ESP32 Marauder backend; RX heartbeat.

## v0.1
- Initial release: Flock/ALPR detection, NFC/RFID audit, GPS geotagging,
  Markdown + DeFlock GeoJSON reports, universal ESP32 companion firmware.
