# FlipDeFlock — 5-Agent Optimization Swarm

Coordination log. Entries follow the 2026 messaging schema (FROM / TO / PHASE /
CONFIDENCE / REFS / body / OUTPUTS_DECLARED / BLOCKING_ON / REVERSIBLE).

## Roster
- **GUARDIAN** — detection logic, OUI/SSID DB, confidence ladder, NFC grading, deauth heuristics
- **WIRE** — ESP32 UART protocol, esp_link, companion FW, Marauder parsing, flasher, baud/port
- **KERNEL** — recon_app.c/.h, Furi idioms, mutex/heap safety, tick cadence, lifecycle (tiebreaker on core files)
- **SHADOW** — BLE anti-stalking, GPS waypoints, geotag policy, BLE categorization
- **DISPATCH** — report schemas (MD/GeoJSON/WiGLE/CSV), SD I/O, settings, CI, docs

---

FROM: HEAD_DEV
TO: ALL
PHASE: HANDOFF
CONFIDENCE: HIGH
REFS: whole repo
Execution model for this environment: the 5 agents run **AUDIT + DESIGN in
parallel, read-only** (no file edits — they return structured findings + concrete
recommended diffs with file:line). HEAD_DEV then resolves any OUTPUTS_DECLARED
overlap (KERNEL has tiebreaker on recon_app.c/.h) and performs IMPLEMENT serially
with a CI build gate after each change, then VERIFY. This keeps the constrained
device safe and every change build-verified.

Standing directives: optimization/enhancement, **not a rewrite**; preserve
passive-only behavior; API **87.1** only; justify every byte (256 KB RAM, FAP
loads fully into RAM); **false positives are worse than missed detections** — no
precision loss without HEAD_DEV sign-off; must build with both `ufbt` and `fbt`.

Already fixed in v0.14–v0.17 (do NOT re-report): flasher 4-byte image alignment,
flash_read/verify `>=`→`>` off-by-one, tx_wait_complete before baud change,
flash_finish+MD5 verify, partial-read loop, delete-partial-backup-on-abort,
RAM stub trim to ESP32-only, fast-baud Safe/Fast option, esp_link stop-command
truncation (tx_wait_complete before UART teardown), device tagging, rogue/
evil-twin (mismatched-security) heuristic, dual-band flockcombo, Marauder Board
Mode + guard screens.
OUTPUTS_DECLARED: AGENT_SWARM.md, SPRINT_SUMMARY.md (this file + summary; agents append findings via HEAD_DEV)
BLOCKING_ON: NONE
REVERSIBLE: YES

---

## AUDIT findings
<!-- HEAD_DEV appends each agent's returned AUDIT/DESIGN entry below. -->

### KERNEL (complete)
Tick = RECON_TICK_MS 250 (recon_app.c:9). Mutex = FuriMutexTypeNormal (non-recursive).
Heap/teardown: **clean** (alloc/free 1:1 on all paths incl. UART-busy + abort). API 87.1: **clean**.

FROM: KERNEL · TO: KERNEL->WIRE · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: views/flock_view.c:35-181 (draw), recon_scene_flock.c:49-51
Top cadence offender: flock_view draw holds app->mutex across ALL canvas/snprintf ops, 4x/sec, stalling the ESP worker. Snapshot visible rows (max 4) + top deauth into locals under the lock, release, then render unlocked.

FROM: KERNEL · TO: KERNEL->WIRE · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: PARTIAL
REFS: recon_scene_wifi.c:51-98 (show_results), recon_scene_ble.c:55-76 (ble_show_results)
Mutex held across O(n^2) evil-twin double-loop + insertion sort (wifi n<=64, ble n<=80); BLE recurs every 4s. Sort/scan a local snapshot, release before the heuristic.

FROM: KERNEL · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: MEDIUM · REVERSIBLE: YES
REFS: recon_app.c:285-288 (recon_settings_load)
esp_baud/gps_baud accept any val>0 — a corrupt huge value is applied to serial init. Clamp to the known-valid sets (115200/921600; gps_baud_val[]).

FROM: KERNEL · TO: SHADOW · PHASE: DESIGN · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: recon_app.c:113-165 (recon_app_ble_add) + 13-66 (recon_app_report_flock)
BLE->Flock merge re-entrancy is safe-but-fragile (release-then-reacquire). Extract an unlocked report_flock_locked() inner so both callers run under one acquire; eliminates the latent self-deadlock.

FROM: KERNEL · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: MEDIUM · REVERSIBLE: YES
REFS: recon_app_i.h:23-26 (array maxes)
Static arrays ~14.6 KB (FlockEntry~64B x96, BleDevice~64B x80, WifiAp~48B x64). Trimming FLOCK_MAX->64, BLE_MAX->48 reclaims ~4 KB if RAM pressure appears. DEFER unless needed.

FROM: KERNEL · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: recon_scene_nfc.c:61-63
NFC scene allocs 3 FuriStrings per tick (leak-free but churny). LOW-value: hoist to scene state. Defer.

### SHADOW (complete)
FROM: SHADOW · TO: HEAD_DEV · PHASE: DESIGN · CONFIDENCE: HIGH · REVERSIBLE: PARTIAL
REFS: recon_app.c:148-154 (following), recon_app_i.h:111-124
Anti-stalking FP fix. Single gate (count>=2 AND moved>100m, latched) misfires on stationary shop Tiles, pass-twice trackers. Replace with AND of 4 (all #define-tunable): count>=4; (last_tick-first_tick)>=90000ms; distinct in-range observer waypoints (>=50m apart) >=3; max track span >=150m. RAISES precision, real stalker still clears easily. Adds ~20 B/device (first_tick,last_tick,inrange_wp_count,last_wp_lat/lon,max_span_m). [HEAD_DEV: behavior change -> implement, note to user.]

FROM: SHADOW · TO: HEAD_DEV · PHASE: DESIGN · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: recon_app.c:56-62 (geotag refresh)
Geotag churn: re-tags on any stronger RSSI; RSSI oscillates +/-5-10 dB -> tag jitter. Add int8_t geotag_rssi to FlockEntry; only move tag if rssi > geotag_rssi + 6 (hysteresis). +96 B total.

FROM: SHADOW · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: N/A
REFS: recon_app.c:98-103 (recon_dist_m)
Distance: planar equirectangular already has cos(lat) term; error <0.5 m at 100 m/45deg, dwarfed by GPS CEP. KEEP planar (haversine adds sinf/atan2f per device/scan for no practical gain). Optional free: constant 111320->111195.

FROM: SHADOW · TO: SHADOW->GUARDIAN · PHASE: DESIGN · CONFIDENCE: MEDIUM (exact IDs LOW)
REFS: flock_companion.ino:318-348 (ble_do_scan classify), recon_app_i.h:30-37 (BleCat)
2026 BLE coverage gap: Google **FMDN (service 0xFEAA)** unhandled — backs Pebblebee/Chipolo/Motorola/Eufy (whole ecosystem reported as plain BLE). HIGH-confidence the class is missing+matters. Add BleCatFindMyDevice -> 0xFEAA. Chipolo native (0xFE33/0xFE79) + Pebblebee exact IDs LOW-confidence -> GUARDIAN verify before shipping. ~6 lines .ino + 1 enum, ~0 RAM.

### WIRE (complete)
FROM: WIRE · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES · **CRITICAL**
REFS: helpers/esp_flasher.c:169 + lib/esp_loader.c:727-736
**Fast-baud flashing is 100% broken.** connect_with_stub sets stub running; we then call the NON-stub esp_loader_change_transmission_rate -> always returns UNSUPPORTED_FUNC -> "Fast 921k" can never connect. **VERIFIED by HEAD_DEV.** Fix: esp_loader_change_transmission_rate_stub(FLASH_BAUD, fast_baud).

FROM: WIRE · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: MEDIUM · REVERSIBLE: YES
REFS: esp_link.c:329-333
Marauder scraper: lines > ESP_LINE_MAX-1 (255) are dropped whole -> every MAC on a long scanap/sniffraw line lost. Bump ESP_LINE_MAX to ~384 (+128 B RAM). Document overflow = bounded whole-line drop.

FROM: WIRE · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: MEDIUM · REVERSIBLE: YES
REFS: esp_link.c:242-249 (marauder_extract_ssid)
SSID extraction returns rest-of-line; trailing fields absorbed into the SSID and fed to flock_ssid_confidence (could spuriously match "flock" later in line). Truncate at first whitespace/comma after the label.

WIRE: teardown race-free, no leaked handles; ESP/GPS independent. No CRC/seq (RAM + lossy passive link). Protocol map captured.

### DISPATCH (complete)
FROM: DISPATCH · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES · **CRITICAL**
REFS: recon_report.c:118-142 (Flock GeoJSON), :254-273 (BLE GeoJSON)
GeoJSON properties don't match DeFlock/OSM tagging -> community-map submissions silently dropped. Use man_made=surveillance, surveillance:type=ALPR, manufacturer=Flock Safety; namespace extras as flipdeflock:*. [HEAD_DEV verify against deflock.me importer.]

FROM: DISPATCH · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES · **CRITICAL**
REFS: recon_report.c:473 (raw SSID), :478-479 (0,0 coords)
WiGLE CSV: (a) SSID written raw -> a comma/quote breaks column count + corrupts the row (also plain/Flock/BLE CSV); add RFC-4180 csv_field_escape(). (b) No-GPS rows written as 0,0 -> WiGLE plants APs at Null Island; OMIT rows without a fix.

FROM: DISPATCH · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: recon_report.c:176-178,:285-286,:493-495 + recon_app.c:271
Secondary report-file write results DISCARDED (only first file's result kept) -> partial write reports "saved". AND every write into ok. settings_save ignores storage_file_write result (silent corrupt settings.txt).

FROM: DISPATCH · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: .github/workflows/build.yml:23
CI: API 87.1 NOT pinned (sdk-channel: release floats); no API-drift guard; no release workflow attaching .fap to v* tags (README tells users to download from releases). Momentum path asserted, not built.

FROM: DISPATCH · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: README.md:50,:130-133
README stale: "ESP Backend" -> renamed "Board Mode"; roadmap lists already-shipped BLE indicators; tagging not in feature list.

FROM: DISPATCH · TO: HEAD_DEV · PHASE: DESIGN · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: recon_report.c:205-292 (save_ble)
ROADMAP: BLE WiGLE export. Same WigleWifi-1.4 format, Type=BLE, ch=0, name->SSID (escaped), last_lat/lon, omit no-fix rows. Factor shared wigle_header().

### GUARDIAN (complete)
FROM: GUARDIAN · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: flock_companion.ino:133-145,451-458 + flock_view.c:59
Deauth flood alerts on a SINGLE frame (no threshold) -> false positives from normal roaming/idle churn. Gate the ESP-side alert on deauth_rate >= DEAUTH_FLOOD_MIN (~5/interval); keep DA attribution unthrottled. (Flipper-side count gate alone drifts since DeauthTarget.count never decays.)

FROM: GUARDIAN · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: flock_db.c:10-19 + flock_companion.ino:45-54
OUI list 31/31 EXACT vs colonelpanichacks/flock-you (no stale/dupes; do NOT bulk-import the unverified "42" set). One genuine miss: **B4:1E:52** = Flock-Safety-registered OUI (per GainSec). Add {0xb4,0x1e,0x52} to both arrays (+3 B flash, const).

FROM: GUARDIAN · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: HIGH · REVERSIBLE: YES
REFS: recon_nfc.c:76-78
NFC: ISO15693-3 graded INFO but SLIX (also 15693) graded WEAK; bare UID-only 15693 access tags are cloneable -> grade WEAK ("UID-only vicinity tag; cloneable"). Precision-first = flag weak credentials.

FROM: GUARDIAN · TO: HEAD_DEV · PHASE: AUDIT · CONFIDENCE: MEDIUM · REVERSIBLE: YES
REFS: wifi_audit.c:91 / recon_nfc ISO14443-4
LOW/judgment: WPA2-PSK graded OK (could be caution); ISO14443-4 "INFO" detail should read as ungraded-needs-inspection. flock_score() wildcard-param + DeauthTarget decay = polish, defer.

---
## HEAD_DEV TRIAGE -> IMPLEMENT
Tier-1 (ship, build-gated): C1 fast-baud stub API; C2 CSV escape + omit no-fix WiGLE rows; C3 GeoJSON OSM tags (verified); H1 AND report write results; H2 baud clamp + settings write check; H5 deauth threshold; H6 B4:1E:52 OUI; H7 NFC 15693->WEAK; H8 geotag hysteresis; H10 ESP_LINE_MAX 384; H11 SSID truncate; M1 FMDN 0xFEAA; M2 BLE WiGLE.
Tier-1.5 (implement + flag user): H9 anti-stalking multi-condition model (raises precision; #define-tunable).
Deferred (note for sign-off, regression risk untested on HW): H3/H4 render/show_results mutex-snapshot refactors (perf, not bugs); CI sdk-pin + release workflow (don't risk green CI); KERNEL array trim; LOW polish items.
