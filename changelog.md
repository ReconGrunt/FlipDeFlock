# Changelog

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
