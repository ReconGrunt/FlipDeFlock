# Changelog

## v0.30
- **Fix "Finalize failed (9)" at the end of a flash.** The write reached 100%
  and every data block was committed to flash, but the final FLASH_END command
  made the ESP32 *ROM* loader answer `COMMAND_FAILED` — a well-known cosmetic
  quirk of the stubless ROM path that does **not** mean the image is bad. The app
  was wrongly treating that as a hard failure and bailing out **before** the MD5
  verify. Now the FLASH_END error is a soft warning, and the **MD5 verify of the
  actual on-chip flash is the real pass/fail gate** — so a good flash reports
  "Verified OK" instead of a scary "Finalize failed".
  - **If you already saw "Finalize failed (9)":** your firmware almost certainly
    wrote fine. Reset the ESP and test it; re-flash with this build to get the
    explicit "Verified OK" confirmation.

## v0.29
- **Fully stubless flasher (fixes "INVALID_COMMAND / software loader is
  resident").** That error appears when the tool tries to upload the flasher
  stub while a stub is already running on the chip (left over from a previous
  attempt). Now **neither** flashing nor backup ever uploads a stub — both go
  through the ESP32 ROM loader, exactly like the 0xchocolate ESP Flasher — so the
  "resident / overlapping address range" error can't happen at all. Backup reads
  via the ROM's 64-byte path (slower, but stub-free and reliable).
  - **If you already hit this:** power-cycle the ESP once (fully remove power) to
    clear the stuck stub, then re-enter bootloader (hold BOOT, tap RESET).

## v0.28
- **Flash over the ROM loader, like the 0xchocolate ESP Flasher (no stub).**
  Flashing a `.bin` now connects straight to the ESP32 ROM bootloader instead of
  uploading the 12.9 KB esp-serial-flasher stub. This matches the widely used,
  proven 0xchocolate flasher, makes the connect lighter/faster, and sidesteps the
  stub's MD5-checked transfer entirely. The write is still verified afterwards.
- **More reliable "Fast" speed.** Lowered the optional fast flash baud from an
  aggressive 921600 to **230400**, which holds up far better over Flipper↔ESP
  wiring (921600 was a likely source of corruption).
- Backup still uses the stub (the ROM can't read flash back) at Safe speed with
  per-chunk retries, and keeps the 5x connect retries from v0.27.

## v0.27
- **Flasher connect + read reliability.** The connection step now retries **5
  times** with a longer per-SYNC timeout and a pause between tries, so a fiddly
  manual bootloader entry (hold BOOT, tap RESET) has many more chances to latch.
- **Backup is now reliable on noisy links.** A "read failed (4)" is an MD5
  mismatch — corrupted bytes in transit, common at the **Fast (921600)** baud.
  Each backup read chunk now **retries** on a transient error, and a backup
  **forces Safe (115200)** speed regardless of the Fast setting, since reads are
  integrity-checked end to end. Flashing (write) keeps the Fast setting — it
  MD5-verifies and retries each block, so a bad fast write is caught and redone.

## v0.26
- **Fix the ESP flasher running the Flipper out of memory.** The in-app flasher
  (Backup / Flash a .bin) could exhaust the Flipper's heap and abort the app —
  a FAP loads entirely into the ~256 KB RAM it shares with the firmware, and the
  flasher's worker (thread stack + UART buffer + the 12.9 KB esp-serial-flasher
  stub) was the tipping point. This was the flasher's first real on-hardware run.
  Fixes: freed runtime heap by right-sizing the scan tables (Flock 96->64,
  BLE 80->48, WiFi 64->48 — still ample for the threat model), halved the
  flasher's transient buffers (4 KB -> 2 KB RX and read-chunk), and added a
  low-RAM pre-flight that shows a clear message instead of crashing if the heap
  is still too tight. No protocol or detection-logic change.

## v0.25
Roadmap sprint, two "Next" items:
- **Raven vs Falcon split.** A Flock unit is now positively identified as a
  **Raven (acoustic/gunshot sensor)** when the companion firmware sees its
  Raven-specific GATT services (`0x3100`–`0x3500`) — shown as
  **"Flock Raven (audio)"** on the BLE detail screen and in reports. The
  external-battery serial is shared across Falcon (ALPR) and Raven, so a Raven
  is asserted **only** on that GATT match; the absence of it is never treated as
  proof of Falcon (a wrong "audio surveillance" label is worse than a generic
  one). New backward-compatible `rv=1` companion line-protocol flag — reflash
  the companion firmware to emit it; older firmware just reports "Flock device".
- **Updatable signature database.** Load extra Flock OUI prefixes and SSID
  patterns from `apps_data/flipdeflock/signatures.json` on the SD card, **merged
  over** the built-ins, so new signatures don't need a rebuild. **Load-only** (no
  writes, no network) and **fail-safe**: a missing or malformed file leaves the
  built-ins fully intact. User signatures are unverified, so an OUI-only hit
  still scores only "possible" — they can add detections, never over-claim.
  Capped for RAM (≤64 OUIs, ≤32 patterns/list). JSON via vendored jsmn (MIT);
  see [docs/signatures.example.json](docs/signatures.example.json).

## v0.24
- DeFlock moved from deflock.me to deflock.org (the old domain redirects).
  Updated all links, the in-app About text, and the Share-to-DeFlock QR handoff
  URL to the canonical domain.
- Added a README "Support" section pointing at the repo Sponsor button.

## v0.23
- **WATCHSCORE coverage honesty.** In Marauder mode (no companion firmware) the
  "am I being watched?" indicator can only see the WiFi/Flock side; the BLE
  tracker, deauth, and evil-twin signals need the companion. It now shows
  **"watch: WiFi only"** and never lets a CLEAR imply Bluetooth/deauth are clear
  too, so a non-flashing user isn't falsely reassured. Flash the companion for
  full coverage.
- Added a "What's new" section to the README.

## v0.22
Roadmap sprint, three new features:
- **Flock BLE serial decode.** Parses the `0x09C8` external-battery advertisement
  to extract a Flock unit's device serial from its always-on battery telemetry
  (no probe/association needed) and shows it on the BLE detail screen. Serial
  logging to reports is **off by default** (Settings → privacy). Note: this
  advert's serial is shared across Falcon (ALPR) and Raven (audio) units, so
  Raven-vs-Falcon isn't split yet. A validated follow-up via the Raven GATT
  service UUIDs is the path.
- **WATCHSCORE.** A single decaying "am I being watched right now?" indicator on
  the start screen that fuses the existing validated signals (confirmed Flock,
  BLE follower, deauth flood, evil-twin AP). **ELEVATED requires ≥2 independent
  radios coinciding**, with hysteresis, dwell, and a per-signal breakdown: one
  state, not an alert flood.
- **Probe IE-fingerprint pipeline (inert until seeded).** The companion firmware
  now hashes each probe-request's Information-Element skeleton and coalesces
  MAC-cycling bursts by 802.11 sequence number, so Flock detection can survive MAC
  randomization. Ships with an empty fingerprint table (no behaviour change, no
  false positives) until seeded from confirmed-unit captures; reports a device
  *class* match, never a unique device.

## v0.21
Roadmap sprint:
- **NFC default-key audit:** on a MIFARE Classic, a new "Deep" check captures the
  UID and tries the Flipper's on-SD key dictionary against every sector, then
  reports how many open with **factory/default keys** (trivially cloneable). This
  answers the core access-control question, "is this badge using default keys?"
  Reads the stock dictionary (no bundled keys); UID and default-keyed count go
  into the report. (mfkey32 deferred: it requires active card emulation.)
- **On-device Flock map:** a live map that plots detected ALPR cameras by
  bearing/distance around your GPS position, with auto-fit zoom, heading tick,
  confidence-by-dot-size, and a scale bar. Visualization only, no new radio
  activity.
- **Share to DeFlock (phone handoff):** renders a QR per marked camera that opens
  DeFlock on your phone at that location, so you contribute through the official
  app's review flow. The Flipper/ESP never touch a network, so passive-only stays
  literally true. (Direct OSM submission deferred: it needs OAuth2/TLS on the ESP,
  which would break the no-network promise.) QR via vendored Nayuki qrcodegen
  (MIT).

## v0.20
- **Anti-stalking precision model** (Tier-2): a BLE tracker is flagged
  "following" only when seen >=4 times over a >=90 s window at >=3 distinct
  observer waypoints spanning >=150 m. This kills urban false positives (a
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
Audit sprint:
- **Flasher:** fast-baud (921600) now works. It was calling the non-stub rate API
  after loading the stub, which always failed.
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
- Clearer support for not flashing (keeping Marauder). Renamed the setting to
  "Board Mode" (Marauder / Companion). In Marauder mode the companion-only
  screens (WiFi Audit, BLE/Tracker Scan) now explain they need the companion
  firmware instead of showing a dead screen, and About shows the active mode and
  what each one does.

## v0.16
- Fix: the ESP board kept scanning after you exited the app. The stop command
  was being cut off because the UART was torn down before it finished
  transmitting; it's now drained first. Works on Marauder and the companion (ships
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
  switching between Marauder and the FlipDeFlock companion, no computer. Built on
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
