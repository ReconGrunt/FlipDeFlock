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
scenes/                  start, flock, flock_detail, nfc, reports, settings, about
views/flock_view.*       custom live-detection list view
helpers/
  flock_db.*             31 OUIs + SSID patterns + confidence scoring
  esp_link.*             ESP32 UART link (companion + generic backends)
  gps_link.*             NMEA GPS reader (2nd UART)
  recon_nfc.*            NFC scanner + security grading
  recon_report.*         Markdown + GeoJSON + CSV writers
esp32_companion/         universal ESP32 firmware + flashing guide
```

Prebuilt binaries are published on the [Releases](../../releases) page and as
per-push CI artifacts, not committed to the repo.

## Credits & data

Detection method and the 31 OUI prefixes build on the open counter-surveillance
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
