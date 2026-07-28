# Updatable signatures (`signatures.json`)

FlipDeFlock ships a trusted, compiled-in detection database (OUIs, SSID patterns,
and — empty by default — probe IE fingerprints). You can **add** to it in the field
without rebuilding by dropping a JSON file on the Flipper's SD card at:

```
apps_data/flipdeflock/signatures.json
```

It's **load-only** (read once at app start, never written, never networked) and
**fail-safe**: if the file is missing, empty, malformed, or oversized, the app
silently falls back to the built-ins — a bad file can't break detection.

Two files ship in this folder, and they are not the same thing:

| File | What it is |
|------|------------|
| [`signatures.example.json`](signatures.example.json) | A **placeholder template**. Its values (`aa:bb:cc`, `deadbeef`, …) match nothing real — copy it and replace them with your own captures. |
| [`signatures.seed.json`](signatures.seed.json) | **Real but unverified** candidate prefixes, tracked upstream and not yet corroborated in the field. See [Seed signatures](#seed-signatures) below before you use it. |

## Schema

All keys are optional; unknown keys are ignored. Every value is an array of strings.

```json
{
  "ouis":           ["aa:bb:cc"],
  "ssid_confirmed": ["flock-"],
  "ssid_likely":    ["flock"],
  "ie_fps":         ["deadbeef"]
}
```

| Key | Meaning | Scores at most | Cap |
|-----|---------|----------------|-----|
| `ouis` | MAC OUI prefix `aa:bb:cc` (case-insensitive) | **Possible** (OUI-only is weak) | 64 |
| `ssid_confirmed` | SSID substring that all but names a Flock unit (e.g. `flock-`) | **Confirmed** | 32 |
| `ssid_likely` | Weaker SSID substring (e.g. `flock`) | **Likely** | 32 |
| `ie_fps` | 8-hex probe **IE fingerprint** (see below) | **"Class?"** | 32 |

Matches are **additive** — your entries can only *add* detections, never remove or
weaken a built-in. Because user signatures are **unverified**, they are deliberately
capped: an OUI never scores above *Possible*, and an IE fingerprint never above the
candidate *"Class?"* rung — **never *Confirmed*, even alongside a Flock OUI**. This
is the precision-over-recall rule: a false "Flock" is worse than a missed one.

## Seed signatures

[`signatures.seed.json`](signatures.seed.json) carries prefixes that are **tracked
upstream but not corroborated** — real candidates, not placeholders, and not proof.
Merge them into your own `signatures.json` if you want the extra recall; leave them
out if you'd rather not chase false positives.

| Prefix | Upstream status | Why it's still unverified |
|--------|-----------------|---------------------------|
| `e0:0a:f6` | Listed *Active*, but with **no confidence note** | Never independently corroborated |
| `14:b5:cd` | *"New finding testing"* (April 2026) | Still under test upstream |

Source: [`nitekry/nite-oui-collection` → `groups/flockers/my_tested_flock.md`](https://github.com/nitekry/nite-oui-collection/blob/main/groups/flockers/my_tested_flock.md),
a per-prefix table with `Confidence` and `Status` columns.

**Why they aren't compiled in.** The built-in OUI table is a claim that a prefix was
*observed in a fielded Flock deployment*. Neither of these meets that bar, so putting
them there would overstate what's known. This file is the honest home for them. The
schema is a flat array of strings and the parser takes no per-entry metadata, so
there is nowhere in the JSON itself to mark an entry unverified — **this page is that
record.**

Either way the scoring is the same: an OUI hit from a user file caps at **Possible**,
exactly as a built-in OUI hit does. Nothing here can manufacture a *Confirmed*.

**Removed from the built-ins:** `f8:a2:d6` was dropped in v0.44. Upstream marks it
**Removed** — "low confidence; hit on a Sony Media Player." Don't add it back through
a user file either. Same for `6c:cd:d6` (Netgear), `94:2a:6f` and `f4:e2:c6`
(Ubiquiti), `cc:cc:cc` (no hits), and `00:0c:e7` (possible false positive) — all
retracted upstream, none ever shipped here.

## Capturing an IE fingerprint (`ie_fps`)

Fielded Flock cameras increasingly phone home as ordinary Wi-Fi **probe requests**
and rotate their MAC to dodge OUI matching. The probe's **IE fingerprint** — the
shape of its 802.11 information elements — is MAC-independent, so it still catches a
unit that has randomized its address.

To seed one from a unit you have **already confirmed** is Flock:

1. Open **Flock / ALPR Detect** and select the confirmed detection to open its
   detail screen.
2. Read the **`IE-fp:`** line (an 8-hex value, e.g. `IE-fp: 1a2b3c4d`). It only
   appears for probe-sourced detections that carried a fingerprint.
3. Add that value to the `ie_fps` array in `signatures.json`.

From then on, a probe with the same IE fingerprint — even from a *different,
randomized* MAC — is flagged **"Class?"** (a candidate device-class match). Only add
a fingerprint you've corroborated against a real deployment; it's a device-*class*
signature, not a unique device ID.

> The placeholder values in `signatures.example.json` (`aa:bb:cc`, `deadbeef`, …)
> are illustrative and won't match anything real — replace them with your own
> captures, and delete any lines you don't need.
