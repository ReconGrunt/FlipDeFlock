#pragma once

#include <core/string.h>

/**
 * NFC/RFID site-survey auditor.
 *
 * Uses the firmware NfcScanner to detect which protocol(s) a presented card
 * supports, then grades the credential's security posture (e.g. MIFARE Classic
 * = broken Crypto1, DESFire = strong if configured). The user logs findings to
 * an SD CSV with timestamp + GPS from the scene.
 *
 * UID capture and active default-key dictionary attacks build on the poller
 * framework and are the planned next iteration.
 */

typedef struct ReconNfc ReconNfc;

/** @param app ReconApp*. */
ReconNfc* recon_nfc_alloc(void* app);
void recon_nfc_free(ReconNfc* nfc);

void recon_nfc_start(ReconNfc* nfc);
void recon_nfc_stop(ReconNfc* nfc);

/**
 * Get the current detection, if any.
 * @param[out] title   short protocol name (e.g. "MIFARE Classic")
 * @param[out] grade   one-word grade ("WEAK"/"MEDIUM"/"STRONG"/"INFO")
 * @param[out] detail  human-readable security notes (multiline)
 * @returns true if a card is currently detected.
 */
bool recon_nfc_get(ReconNfc* nfc, FuriString* title, FuriString* grade, FuriString* detail);
