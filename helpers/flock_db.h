/**
 * @file flock_db.h
 * Flock Safety / ALPR surveillance-device detection database and scoring.
 *
 * Pure logic, no firmware dependencies, so it can be unit-tested on a host.
 *
 * Detection data is sourced from the open-source counter-surveillance research
 * projects (colonelpanichacks/flock-you, 0xXyc/flock-you-wifi-recon) and the
 * DeFlock community (deflock.me). The OUI prefixes are generic vendor prefixes
 * observed in fielded Flock deployments, so an OUI match alone is "possible",
 * not "confirmed" -- behaviour (probe requests) and SSID naming raise the
 * confidence. We never present an OUI-only hit as certain.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How sure we are that a wireless device is Flock/ALPR surveillance gear. */
typedef enum {
    FlockConfidenceNone = 0, /**< No indicators matched. */
    FlockConfidencePossible, /**< OUI prefix match only (generic vendor prefix). */
    FlockConfidenceLikely, /**< OUI + phone-home probe behaviour, or "flock/flck" substring. */
    FlockConfidenceProbeFp, /**< B1: probe IE-fingerprint matched a curated Flock
                              *   device-CLASS hash (survives MAC randomization).
                              *   A candidate class match, NOT a unique device --
                              *   sits above a loose OUI+probe "Likely" but below a
                              *   near-unique SSID "Confirmed". */
    FlockConfidenceConfirmed, /**< SSID matches a known Flock naming pattern. */
} FlockConfidence;

/**
 * B1: match a probe-request IE-skeleton FNV-1a hash (from the companion's
 * `,fp=<hex32>` field) against a curated table of confirmed-Flock fingerprints.
 *
 * PRECISION GUARD: the table ships EMPTY/inert. We do not yet have validated
 * confirmed-Flock IE-fp captures, so nothing matches and there is zero behaviour
 * change (zero false positives) until real seeds are added. This honours
 * precision-over-recall: a false "Flock" is worse than a missed one. The match
 * is a device-CLASS / firmware-stack signature, never a unique device ID.
 *
 * @param fp  IE-skeleton hash; 0 means "no fingerprint" and never matches.
 * @return    true only if `fp` is in the curated table (currently always false).
 */
bool flock_ie_fp_match(uint32_t fp);

/** Number of known Flock-associated OUI prefixes. */
size_t flock_oui_count(void);

/** Get the i-th OUI prefix (3 bytes) for display. Returns NULL if out of range. */
const uint8_t* flock_oui_get(size_t index);

/** True if the first 3 bytes of `mac` match a known Flock-associated OUI. */
bool flock_oui_match(const uint8_t* mac);

/**
 * Confidence contributed by an SSID string alone (may be NULL/empty for hidden
 * networks or probe requests with no SSID).
 */
FlockConfidence flock_ssid_confidence(const char* ssid);

/**
 * Combined confidence for an observed device.
 *
 * @param mac          6-byte MAC of the transmitter (must not be NULL).
 * @param ssid         Advertised/probed SSID, or NULL/"" if unknown.
 * @param is_probe_req True if this frame is a station-mode probe request
 *                     (the "phoning home" behaviour Flock cameras exhibit).
 */
FlockConfidence flock_score(const uint8_t* mac, const char* ssid, bool is_probe_req);

/** Human-readable label for a confidence level. */
const char* flock_confidence_str(FlockConfidence confidence);

#ifdef __cplusplus
}
#endif
