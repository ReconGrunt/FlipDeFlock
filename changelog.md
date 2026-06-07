# Changelog

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
