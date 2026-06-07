<p align="center">
  <img src="assets/logo.png" width="560" alt="FlipDeFlock">
</p>

A Flipper Zero app that turns your Flipper + **any ESP32 board** into a
counter-surveillance **site-survey** tool:

- 🛰️ **Flock / ALPR Detect** — find Flock Safety / ALPR surveillance cameras
  over **Wi-Fi *and* BLE** (the companion FW interleaves a 2.4 GHz sniff with a
  BLE scan), geotag them with GPS, and mark them for a report.
- 📶 **WiFi Audit** — scan nearby networks and grade each one's security
  (Open/WEP/WPA1/WPA2/WPA3, WPS, TKIP, hidden), spelling out exactly what's
  weak, and flag **evil-twin** SSIDs (same name, multiple BSSIDs). Plus passive
  **deauth/disassoc-flood detection** that alerts live on the Detect screen.
  *Requires the companion firmware* (Marauder doesn't emit encryption over serial).
- 📡 **BLE / Tracker Scan** — detect Flock/Raven BLE beacons and **AirTag / Tile
  / SmartTag trackers**, and flag any tracker that **follows you across GPS
  waypoints** (anti-stalking). Companion FW.
- 💳 **NFC / RFID Audit** — identify a presented card's protocol and grade its
  security posture for access-control reviews.
- ⚡ **ESP32 Firmware** — **back up** the board's current firmware to SD and
  **flash a `.bin`** (companion / Marauder / a backup) straight from the Flipper,
  no computer. Bounce between Marauder and the FlipDeFlock companion. (Built on
  Espressif's esp-serial-flasher; put the ESP in bootloader mode first.)
- 🗺️ **Reports** — export to Markdown, **DeFlock-compatible GeoJSON** (ready to
  contribute to [deflock.me](https://deflock.me)), CSV, and **WiGLE CSV**
  (wardriving standard) on the SD card.

> **Passive recon only.** No deauth, no injection, no jamming. For lawful,
> authorized use: your own security assessments, anti-surveillance awareness,
> CTF/research. OUI-only matches are *possible*, not confirmed — verify by eye.

Built for the [Momentum firmware](https://github.com/Next-Flip/Momentum-Firmware)
(works on stock OFW too).

## Why an ESP32?

The Flipper's onboard radio is BLE-only and can't do Wi-Fi monitor mode. Flock
cameras are most reliably found by the Wi-Fi probe requests they constantly spray
trying to phone home — so the Wi-Fi work runs on an ESP32 and the Flipper is the
UI, GPS-tagger and logger.

**Universal — works with any ESP32 Flipper board** (Wi-Fi Dev Board, ESP32
Marauder boards, ReksLab Tri-Board, bare WROOM/WROVER, Xiao ESP32-S3, …):

1. **Companion FW** (`esp32_companion/`): flash our tiny sketch to any ESP32 for
   a clean, low-noise line protocol. Recommended.
2. **Marauder / Generic**: keep your existing firmware (e.g. ESP32 Marauder) —
   the app scrapes MAC/SSID tokens from whatever it prints and applies the Flock
   filter on the Flipper. Set *Board Mode = Marauder* in Settings.

## Wiring

| Device | Flipper port | Pins |
|--------|--------------|------|
| ESP32  | USART  | 13 (TX) / 14 (RX), 3V3, GND |
| GPS    | LPUART | 15 / 16 |

Both run at the same time. Ports/bauds are configurable in Settings for boards
with nonstandard pinouts.

## Install (prebuilt)

Download `flipdeflock.fap` from the
[latest release](../../releases/latest), copy it to `apps/Tools/` on your
Flipper's SD card, and launch **FlipDeFlock** from the Tools menu.

> The release `.fap` targets Flipper API **87.1**, shared by current stock OFW
> and Momentum. If a future firmware bumps the API and it refuses to load
> ("API mismatch"), just rebuild for your firmware with `ufbt` (below). Every
> push also builds a fresh `.fap` as a CI artifact under the **Actions** tab.

## Using the app

Wire up your ESP32 (and optional GPS) per [Wiring](#wiring), launch **FlipDeFlock**
from `Apps → Tools`, and start with **Settings**.

### 1. Pick your Board Mode (Settings)

Open **Settings** and set **Board Mode** to match your ESP32:

- **Marauder** — keep your board's existing firmware, *no flashing*. You get
  **Flock/ALPR Detect + NFC + GPS + Reports**.
- **Companion** — our firmware (flash it with **ESP32 Firmware**, below). Adds
  **WiFi Audit, BLE/Tracker Scan, deauth detection, and dual-band Flock**.

While here, check **ESP Port/Baud** and **GPS** if your wiring differs from the
defaults, and turn **GPS** on if you want detections geotagged. Settings persist.

### 2. Flock / ALPR Detect

The main camera hunt. It shows a live list as the ESP32 sniffs:

- Each row is a detection with a **confidence** tag — `Possible` / `Likely` /
  `CONFIRMED` (see [confidence](#how-detection-confidence-works)) — and the
  detection source in the detail view (`probe` / `beacon` / `BLE`).
- A `!DEAUTH ch<n> <bssid>` banner appears if a real deauth **flood** is detected
  (it clears when the flood stops).
- Press **OK** on a row to open its detail; press **OK** again (the **Mark**
  button) to flag it for the report. Press **Back** to return; the ESP goes idle
  when you leave.

### 3. WiFi Audit *(Companion only)*

Scans nearby networks and grades each one's security worst-first. Markers:
`!` = rogue/evil-twin (same SSID, mismatched security), `~` = duplicate SSID,
`*` = tagged. Open a row for the full breakdown (auth/cipher, WPS, vendor, and
exactly what's weak). Use **Save Report** to write it out. *(In Marauder mode
this screen explains it needs the companion firmware.)*

### 4. BLE / Tracker Scan *(Companion only)*

Continuously scans for **AirTag / Tile / SmartTag / Google Find My** trackers and
Flock/Raven BLE. With **GPS on**, a tracker that stays with you across several
waypoints is flagged **`!FOLLOWING`** (anti-stalking) — open it to see the track
(distance / waypoints / time). **Tag** suspicious devices for the report.

### 5. NFC / RFID Audit

Present a card to the Flipper; the app identifies its protocol and grades the
security posture (e.g. UID-only / cloneable vs. authenticated) for access-control
reviews.

### 6. ESP32 Firmware — backup & flash

Manage your board's firmware from the Flipper, no computer:

1. **Backup current FW → SD** *(do this first!)* — dumps the whole ESP32 flash to
   `apps_data/flipdeflock/firmware/` so you can restore Marauder later. It's
   read-only and safe.
2. **Flash a .bin** — pick the companion `flock_companion-merged.bin` (from a
   release, copied to SD), a backup, or any merged image; it writes at `0x0`.

When prompted, put the ESP32 into **bootloader/download mode** (hold **BOOT**, tap
**RESET**), then it connects. Flash speed is **Safe (115200)** or **Fast (921600)**
in Settings. You can't brick it — the ROM bootloader always allows a re-flash.

### 7. Reports

Saved reports land on the SD under **`apps_data/flipdeflock/reports/`**:
Markdown (human-readable), **DeFlock-compatible GeoJSON** (ready for
[deflock.me](https://deflock.me)), KML, plain CSV, and **WiGLE CSV** (WiFi *and*
BLE) for wardriving uploads. Pull them with qFlipper or a card reader.

## Build from source

**With [ufbt](https://pypi.org/project/ufbt/) (standalone, recommended):**

```sh
pip install ufbt
ufbt            # builds flipdeflock.fap in dist/
ufbt launch     # build + install + run on a connected Flipper
```

**Inside a Momentum/OFW tree:** drop this folder into `applications_user/` and
run `./fbt fap_flipdeflock`.

**ESP32 companion:** see [`esp32_companion/README.md`](esp32_companion/README.md)
for Arduino IDE / arduino-cli flashing.

## How detection confidence works

Flock-associated OUIs are generic vendor prefixes, so the app never cries wolf:

| Signal | Confidence |
|--------|------------|
| OUI prefix only | `Possible` |
| OUI + phone-home probe request | `Likely` |
| SSID matches `Flock-XXXXXX` / `test_flck` | `CONFIRMED` |

## Layout

```
application.fam          manifest
recon_app.c / _i.h       lifecycle, shared state, settings
scenes/                  start, flock, wifi, ble, nfc, firmware, reports, settings, about
views/flock_view.*       custom live-detection list view
helpers/
  flock_db.*             Flock OUIs + SSID patterns + confidence scoring
  esp_link.*             ESP32 UART link (companion + generic backends)
  esp_flasher.*          in-app ESP32 backup/flash (esp-serial-flasher port)
  gps_link.*             NMEA GPS reader (2nd UART)
  recon_nfc.*            NFC scanner + security grading
  recon_report.*         Markdown + GeoJSON + KML + CSV/WiGLE writers
lib/esp-serial-flasher/  vendored Espressif flasher (Apache-2.0)
esp32_companion/         universal ESP32 firmware + flashing guide
```

Prebuilt binaries are published on the [Releases](../../releases) page and as
per-push CI artifacts, not committed to the repo.

## Credits & data

Detection method and the Flock OUI prefixes build on the open counter-surveillance
work of [colonelpanichacks/flock-you](https://github.com/colonelpanichacks/flock-you),
[0xXyc/flock-you-wifi-recon](https://github.com/0xXyc/flock-you-wifi-recon), and
the [DeFlock](https://deflock.me) community. Thanks to the researchers who mapped
these signatures. GPS NMEA approach inspired by the Momentum Sub-GHz GPS helper.

## License

MIT — see [LICENSE](LICENSE).

## Roadmap

- NFC: capture UID + run default-key / mfkey32 dictionary checks (poller framework).
- Flock: on-device map view.
- Direct DeFlock submission (needs a phone/network bridge).

Shipped since the early roadmap: dual-band Wi-Fi+BLE Flock detection, BLE
anti-stalking tracker scan, WiFi security audit, deauth attribution, device
tagging, WiGLE export (WiFi + BLE), and an in-app ESP32 backup/flasher. See
[changelog.md](changelog.md).
