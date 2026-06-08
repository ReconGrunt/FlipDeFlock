<p align="center">
  <img src="assets/logo.png" width="560" alt="FlipDeFlock">
</p>

A Flipper Zero app that pairs your Flipper with **any ESP32 board** for
counter-surveillance site surveys:

- 🛰️ **Flock / ALPR Detect** — finds Flock Safety / ALPR cameras over Wi-Fi and
  BLE (the companion firmware interleaves a 2.4 GHz sniff with a BLE scan),
  geotags them with GPS, and marks them for a report.
- 🗺️ **On-device Map** — a live Flipper-screen map plotting detected cameras by
  bearing and distance around your GPS position (auto-fit, heading, scale bar).
  Lets you see what's nearby in the field without exporting.
- 📶 **WiFi Audit** — scans nearby networks and grades each one's security
  (Open/WEP/WPA1/WPA2/WPA3, WPS, TKIP, hidden), spelling out what's weak, and
  flags **evil-twin** SSIDs (same name, multiple BSSIDs). Also does passive
  deauth/disassoc-flood detection that alerts live on the Detect screen.
  *Requires the companion firmware* (Marauder doesn't emit encryption over serial).
- 📡 **BLE / Tracker Scan** — detects Flock/Raven BLE beacons and AirTag / Tile /
  SmartTag trackers, and flags any tracker that follows you across GPS waypoints
  (anti-stalking). Positively labels a **Raven (audio sensor)** when it sees the
  Raven's own Bluetooth services. Companion firmware.
- 💳 **NFC / RFID Audit** — identifies a presented card's protocol and grades its
  security posture for access-control reviews. On a MIFARE Classic, a **Deep**
  check captures the UID and tries the Flipper's on-SD key dictionary to report
  how many sectors open with factory/default keys (trivially cloneable).
- ⚡ **ESP32 Firmware** — backs up the board's current firmware to SD and flashes
  a `.bin` (companion / Marauder / a backup) straight from the Flipper, no
  computer. Lets you switch between Marauder and the FlipDeFlock companion. (Built
  on Espressif's esp-serial-flasher; put the ESP in bootloader mode first.)
- 🗺️ **Reports** — exports to Markdown, DeFlock-compatible GeoJSON (ready to
  contribute to [deflock.org](https://deflock.org)), CSV, and WiGLE CSV (the
  wardriving format) on the SD card.
- 📲 **Share to DeFlock** — renders a QR per camera that opens DeFlock on your
  phone at that location, so you contribute through the official app. The
  Flipper and ESP never touch a network; submission stays an off-device,
  phone-side action, which keeps the passive-only promise intact. No Flipper GPS
  is needed to contribute — DeFlock lets you place the pin by hand.

> **Passive recon only.** No deauth, no injection, no jamming. For lawful,
> authorized use: your own security assessments, anti-surveillance awareness,
> CTF/research. OUI-only matches are *possible*, not confirmed. Verify by eye.

Built for the [Momentum firmware](https://github.com/Next-Flip/Momentum-Firmware)
(works on stock OFW too).

## Status — a work in progress

FlipDeFlock is actively developed, not a finished product. It's already useful in
the field, but features are still landing, the detection signatures change as
surveillance hardware changes, and not every path is hardware-tested on every
board. Expect rough edges and the occasional breaking change between versions.
Treat detections as indicators, not proof, and verify by eye. If you're relying on
it for anything that matters, read the code and confirm the behavior yourself.
Feedback and field data are what move it forward; see [Contributing](#contributing).

## What's new

**v0.33**
- **Robustness pass.** A multi-agent code audit hardened the report writers and the
  NFC deep check: exports (CSV / GeoJSON / KML / WiGLE) now stay valid even when a
  network SSID or a Bluetooth tracker name contains an odd character, the NFC
  default-key check fails cleanly instead of crashing on a tight heap, and a failed
  save no longer leaves a half-written report behind. No change to detection logic.

**v0.25**
- **Tells a Raven (audio) from a Falcon (camera).** A Flock pole carries either an
  ALPR camera (Falcon) or an acoustic/gunshot sensor (Raven) — both share the same
  Bluetooth battery, so the battery alone can't tell them apart. When the companion
  firmware sees a Raven's own Bluetooth services, the app now labels it
  **"Flock Raven (audio)"** outright. It only says "Raven" on that positive signal
  — it never *guesses* "camera" just because the Raven services are missing, so it
  won't mislabel a pole. (Reflash the companion to pick up the new signal; older
  firmware just shows "Flock device.")
- **Update detection signatures without a new release.** Drop a
  `signatures.json` on the SD card at `apps_data/flipdeflock/` to add your own
  Flock OUI prefixes and SSID patterns on top of the built-in list. It's
  **read-only and offline** — the app never writes the file or phones home — and
  **fail-safe**: a missing or broken file just falls back to the built-ins.
  Your additions can only *add* detections, never override the precision rules.
  See [docs/signatures.example.json](docs/signatures.example.json).

**v0.22**
- **"Am I being watched right now?" indicator.** A single home-screen status
  (**CLEAR / WATCHFUL / ELEVATED**) that fuses the app's alerts — a Flock camera
  nearby, a Bluetooth tracker following you, a deauth attack, an evil-twin WiFi —
  into one light instead of four separate beeps. It only goes **ELEVATED** when
  two different radios agree, so it doesn't cry wolf. *Coverage note:* the
  Bluetooth and deauth signals only exist with the companion firmware. In
  **Marauder mode** it can only watch the WiFi/Flock side, so it shows
  **"watch: WiFi only"**; a "clear" there means "nothing on WiFi," not "no one's
  watching." Flash the companion for full coverage.
- **Flock unit serial readout.** Reads a Flock camera's serial from its always-on
  Bluetooth battery beacon (shown on the BLE detail screen; logging to reports is
  off by default for privacy).
- **MAC-randomization defense (groundwork).** The companion now fingerprints the
  shape of a camera's WiFi probe, not just its hardware address, so detection can
  survive Flock scrambling MACs. Ships dormant until validated against a confirmed
  unit, so it can't raise false alarms before it's proven.

**v0.21** — On-device map of detected cameras, NFC default-key audit, and
Share-to-DeFlock QR phone-handoff.
**v0.20** — Tighter anti-stalking: a tracker is only flagged as "following" after
a real multi-waypoint track, not a single drive-by.

Full history in [changelog.md](changelog.md).

## Why an ESP32?

The Flipper's onboard radio is BLE-only and can't do Wi-Fi monitor mode. Flock
cameras are most reliably found by the Wi-Fi probe requests they constantly spray
trying to phone home, so the Wi-Fi work runs on an ESP32 and the Flipper acts as
the UI, GPS-tagger and logger.

Works with any ESP32 Flipper board (Wi-Fi Dev Board, ESP32 Marauder boards,
ReksLab Tri-Board, bare WROOM/WROVER, Xiao ESP32-S3, and so on):

1. **Companion FW** (`esp32_companion/`): flash the sketch to any ESP32 for a
   clean, low-noise line protocol. Recommended.
2. **Marauder / Generic**: keep your existing firmware (e.g. ESP32 Marauder). The
   app scrapes MAC/SSID tokens from whatever it prints and applies the Flock
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

- **Marauder** — keep your board's existing firmware, no flashing. You get
  **Flock/ALPR Detect + NFC + GPS + Reports**.
- **Companion** — the project firmware (flash it with **ESP32 Firmware**, below).
  Adds **WiFi Audit, BLE/Tracker Scan, deauth detection, and dual-band Flock**.

While here, check **ESP Port/Baud** and **GPS** if your wiring differs from the
defaults, and turn **GPS** on if you want detections geotagged. Settings persist.

### 2. Flock / ALPR Detect

The main camera hunt. It shows a live list as the ESP32 sniffs:

- Each row is a detection with a **confidence** tag (`Possible` / `Likely` /
  `CONFIRMED`, see [confidence](#how-detection-confidence-works)) plus the
  detection source in the detail view (`probe` / `beacon` / `BLE`).
- A `!DEAUTH ch<n> <bssid>` banner appears if a real deauth **flood** is detected,
  and clears when the flood stops.
- Press **OK** on a row to open its detail; press **OK** again (the **Mark**
  button) to flag it for the report. Press **Back** to return; the ESP goes idle
  when you leave.

### 3. Flock Map

A live map around your GPS position: you're at center, detected cameras are
plotted by bearing and distance (dot size = confidence), with a heading tick and
a scale bar. **Left/Right** zoom, **OK** re-fits. Needs a GPS fix; cameras without
a geotag aren't plotted.

### 4. WiFi Audit *(Companion only)*

Scans nearby networks and grades each one's security worst-first. Markers:
`!` = rogue/evil-twin (same SSID, mismatched security), `~` = duplicate SSID,
`*` = tagged. Open a row for the full breakdown (auth/cipher, WPS, vendor, and
exactly what's weak). Use **Save Report** to write it out. *(In Marauder mode
this screen explains it needs the companion firmware.)*

### 5. BLE / Tracker Scan *(Companion only)*

Continuously scans for **AirTag / Tile / SmartTag / Google Find My** trackers and
Flock/Raven BLE. With **GPS on**, a tracker that stays with you across several
waypoints is flagged **`!FOLLOWING`** (anti-stalking); open it to see the track
(distance / waypoints / time). **Tag** suspicious devices for the report.

### 6. NFC / RFID Audit

Present a card to the Flipper; the app identifies its protocol and grades the
security posture (e.g. UID-only / cloneable vs. authenticated) for access-control
reviews. If it's a **MIFARE Classic**, a **Deep** softkey appears: it captures
the UID and tries the Flipper's on-SD key dictionary against every sector, then
reports how many open with **default keys** (`N/total` = trivially cloneable).
Audit only cards you own or are authorized to test.

### 7. ESP32 Firmware — backup & flash

Manage your board's firmware from the Flipper, no computer:

1. **Backup current FW → SD** *(do this first!)* — dumps the whole ESP32 flash to
   `apps_data/flipdeflock/firmware/` so you can restore Marauder later. It's
   read-only and safe.
2. **Flash a .bin** — pick the companion `flock_companion-merged.bin` (from a
   release, copied to SD), a backup, or any merged image; it writes at `0x0`.

When prompted, put the ESP32 into **bootloader/download mode** (hold **BOOT**, tap
**RESET**), then it connects. Flash speed is **Safe (115200)** or **Fast (921600)**
in Settings. You can't brick it: the ROM bootloader always allows a re-flash.

### 8. Reports

Saved reports land on the SD under **`apps_data/flipdeflock/reports/`**:
Markdown (human-readable), DeFlock-compatible GeoJSON (ready for
[deflock.org](https://deflock.org)), KML, plain CSV, and WiGLE CSV (WiFi and BLE)
for wardriving uploads. Pull them with qFlipper or a card reader.

### 9. Share to DeFlock (phone handoff)

For each camera you **marked** (and that has a GPS geotag), this screen shows a
QR code. Scan it with your phone to open DeFlock at that location and submit
through the official app's review flow. **Left/Right** pages between cameras; the
lat/lon and OSM tags are shown on-screen too. The Flipper and ESP never connect
to a network, so the submission happens entirely on your phone.

**No GPS module on your Flipper?** You can still contribute. DeFlock does not
require GPS — its app and web editor let you place and drag the camera's pin on
the map by hand. Stand at the camera, open
[deflock.org/report](https://deflock.org/report) (or the DeFlock app), and drop
the pin on it. The QR handoff only lists cameras the Flipper geotagged, so without
a fix you add those directly on DeFlock.

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

Flock-associated OUIs are generic vendor prefixes, so a prefix match alone is weak
evidence. Confidence is scored accordingly:

| Signal | Confidence |
|--------|------------|
| OUI prefix only | `Possible` |
| OUI + phone-home probe request | `Likely` |
| SSID matches `Flock-XXXXXX` / `test_flck` | `CONFIRMED` |

## Layout

```
application.fam          manifest
recon_app.c / _i.h       lifecycle, shared state, settings
scenes/                  start, flock, map, wifi, ble, nfc, firmware, reports,
                         deflock_handoff, settings, about
views/
  flock_view.*           custom live-detection list view
  flock_map_view.*       on-device map (operator-centered camera plot)
  deflock_qr_view.*      QR render for the DeFlock phone-handoff
helpers/
  flock_db.*             Flock OUIs + SSID patterns + confidence scoring
  esp_link.*             ESP32 UART link (companion + generic backends)
  esp_flasher.*          in-app ESP32 backup/flash (esp-serial-flasher port)
  gps_link.*             NMEA GPS reader (2nd UART)
  recon_nfc.*            NFC scanner + grading + default-key deep check
  recon_report.*         Markdown + GeoJSON + KML + CSV/WiGLE writers
lib/esp-serial-flasher/  vendored Espressif flasher (Apache-2.0)
lib/qrcodegen/           vendored Nayuki QR Code generator (MIT)
esp32_companion/         universal ESP32 firmware + flashing guide
```

Prebuilt binaries are published on the [Releases](../../releases) page and as
per-push CI artifacts, not committed to the repo.

## Credits & data

The detection method and Flock OUI prefixes build on the open
counter-surveillance work of
[colonelpanichacks/flock-you](https://github.com/colonelpanichacks/flock-you),
[0xXyc/flock-you-wifi-recon](https://github.com/0xXyc/flock-you-wifi-recon), and
the [DeFlock](https://deflock.org) community. Thanks to the researchers who mapped
these signatures. The GPS NMEA approach is based on the Momentum Sub-GHz GPS
helper.

## License

MIT — see [LICENSE](LICENSE).

## Support

FlipDeFlock is free and MIT-licensed, and it stays that way. If it's useful and
you want to help cover development (and any legal costs that come with mapping
surveillance hardware), there's a **Sponsor** button at the top of the repo.
Donations are optional and never gate a feature. Spreading the word, filing good
detections, and sending patches help just as much — see [Contributing](#contributing).

## Contributing

Contributions are welcome. This is a community counter-surveillance effort, and it
improves with more eyes, more boards, and more field data. The most useful things
you can do:

- **Field reports & signatures** — new Flock/ALPR OUIs, SSID/BLE patterns, or
  false positives and misses you hit in the wild. Precision feedback is the most
  valuable. You can test a candidate signature on your own device first by adding
  it to `apps_data/flipdeflock/signatures.json` (see
  [docs/signatures.example.json](docs/signatures.example.json)), then send the
  ones that hold up.
- **Board support** — try it on your ESP32 hardware and report wiring/quirks.
- **Code** — bug fixes, new report formats, or any of the deferred roadmap items.

Open an issue or a pull request. A few ground rules keep the project coherent:

- **Passive recon only.** No deauth, injection, or jamming, ever.
- **Correctness over features.** A false positive is worse than a missed
  detection; don't trade precision for recall without good reason.
- Target **API 87.1**, and it must build with both `ufbt` and `fbt`.
- Keep it lean. The `.fap` loads entirely into the Flipper's 256 KB of RAM.

By contributing you agree to license your work under this repo's MIT license.

## Roadmap

Planned work, deferred items (with reasons), and the community/funding track are
in [ROADMAP.md](ROADMAP.md). Shipped history is in [changelog.md](changelog.md).
