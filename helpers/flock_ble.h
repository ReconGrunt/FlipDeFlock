/**
 * @file flock_ble.h
 * Decoder for the Flock Safety external-battery BLE advert (mfg id 0x09C8,
 * XUNTONG). Extracts the always-on ASCII device serial and makes a CONSERVATIVE
 * model guess (Falcon ALPR vs Raven acoustic sensor).
 *
 * Pure logic, no firmware dependencies, so it can be unit-tested on a host.
 *
 * Sourced from open counter-surveillance research: ryanohoro's Falcon teardown
 * (the "TN7..." serial inside the XUNTONG mfg data, and the legacy
 * "Penguin-NNNN" / "FS Ext Battery" GAP name) and colonelpanichacks/flock-you.
 *
 * IMPORTANT: the serial belongs to the *shared* external-battery unit that
 * Flock co-deploys on BOTH the Falcon and the Raven, so a Raven-vs-Falcon split
 * is NOT currently derivable from this advert. The model guess therefore stays
 * generic until a serial-prefix mapping is field-validated -- see
 * flock_ble_model() in flock_ble.c.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Conservative Flock BLE model guess from the 0x09C8 advert. */
typedef enum {
    FlockBleModelUnknown = 0, /**< Nothing decoded as a Flock battery unit. */
    FlockBleModelGeneric, /**< Flock external battery -- model not determinable. */
    FlockBleModelFalcon, /**< NEEDS VALIDATION: ALPR camera (never emitted yet). */
    FlockBleModelRaven, /**< NEEDS VALIDATION: acoustic sensor (never emitted yet). */
} FlockBleModel;

/**
 * Extract the ASCII device serial from the 0x09C8 manufacturer payload, falling
 * back to a bare alphanumeric GAP name (post-2025-03 firmware drops "Penguin-").
 *
 * @param mfg         Raw manufacturer-specific data INCLUDING the 2-byte LE
 *                    company id (may be NULL/empty if only the name is known).
 * @param len         Length of @p mfg in bytes.
 * @param name        GAP device name, or NULL/"" if unknown.
 * @param out_serial  Receives a NUL-terminated serial (cleared on failure).
 * @param serial_cap  Capacity of @p out_serial in bytes.
 * @return true if a plausible serial was extracted.
 */
bool flock_ble_extract_serial(
    const uint8_t* mfg,
    size_t len,
    const char* name,
    char* out_serial,
    size_t serial_cap);

/**
 * Conservative model guess. CURRENTLY ALWAYS returns Generic/Unknown -- the
 * Raven-vs-Falcon serial-prefix mapping is unvalidated. See the body for where
 * to add prefix checks once corroborated.
 */
FlockBleModel flock_ble_model(const char* serial, const char* name);

/** Human-readable label (Raven/Falcon labels carry a "?" uncertainty marker). */
const char* flock_ble_model_str(FlockBleModel model);

#ifdef __cplusplus
}
#endif
