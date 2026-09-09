// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
/**
 * @file survey_rank.h
 * Ranks the probe survey so the operator can see what is in the air.
 *
 * Pure logic, no firmware dependencies, so it can be unit-tested on a host.
 *
 * WHY THIS EXISTS (issue #25). Every modern Flock camera randomises its MAC, so
 * its address is locally administered, belongs to no manufacturer, and matches
 * no OUI table -- ours or anyone else's. The detector only reports what it
 * recognises, so an operator parked next to a camera gets an empty screen. That
 * is exactly what happened: a reporter drove past ten-plus cameras, saw nothing,
 * and the camera was only found afterwards by reading survey.csv off the card by
 * hand and noticing one row probing ten times at -26 dBm while everything around
 * it managed one or two.
 *
 * That reasoning is mechanical, and this file is it. The survey was already
 * being collected and written to the card; it was simply never shown on the
 * device, so the one thing that could break the deadlock was invisible in the
 * field and legible only to someone with the CSV and the context.
 *
 * WHAT THIS IS NOT. Not a detection, not a confidence rung, and it never enters
 * the Flock hit table. Precision over recall means the app must not start
 * announcing cameras it cannot substantiate, and a high rank here means only
 * "this transmitter behaves the way a fixed camera behaves" -- which a busy
 * access point also does. It answers "where should I point my eyes", and the
 * operator supplies the ground truth by looking.
 *
 * THE SCORE IS RELATIVE TO THE CAPTURE, deliberately. Ten probes is unremarkable
 * on a busy street and damning on an empty one. What identified the camera in
 * #25 was not the absolute count but that it was five times its neighbours'. So
 * persistence is scored against the busiest row present, the same comparison a
 * human makes reading the file.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One surveyed transmitter, firmware-free so the host tests can build it.
 *
 * Mirrors the fields of SurveyEntry (recon_app_i.h) rather than including it,
 * because that header pulls in the whole firmware SDK. The scene copies the
 * handful of fields across.
 */
typedef struct {
    uint8_t mac[6];
    uint32_t fp; /**< IE-skeleton hash, 0 = none captured */
    int8_t rssi; /**< strongest seen, i.e. closest approach */
    uint8_t channel;
    uint16_t count; /**< probes seen; a camera probes on, a phone bursts and stops */
} SurveyRankRow;

/** Why a row ranked where it did. Shown to the operator; never a claim. */
typedef enum {
    SurveyEvidenceNone = 0,
    SurveyEvidenceLocalAdmin = 1 << 0, /**< randomised address -- no OUI can ever match */
    SurveyEvidencePersistent = 1 << 1, /**< keeps probing rather than bursting once */
    SurveyEvidenceClose = 1 << 2, /**< strong signal, so physically near */
    SurveyEvidenceRotating = 1 << 3, /**< same fingerprint seen on 2+ addresses */
    SurveyEvidenceGeneric = 1 << 4, /**< commodity scan skeleton -- actively uninteresting */
} SurveyEvidence;

/** A scored row: the input, its score, and the evidence behind it. */
typedef struct {
    size_t index; /**< position in the caller's input array */
    uint8_t score; /**< 0-100, higher = more worth looking at */
    uint8_t evidence; /**< bitmask of SurveyEvidence */
} SurveyRanked;

/**
 * The RSSI at or above which a row is called "close", in dBm.
 *
 * -55 dBm is roughly "same side of the street". The camera in #25 was -26 and
 * the neighbour's set-top box was -87, so the gap this has to separate is wide
 * and the exact cut is not delicate.
 */
#define SURVEY_CLOSE_DBM (-55)

/**
 * Probe count at or above which a row is called "persistent".
 *
 * Three, because one or two probes is a device waking up, glancing at the band
 * and going quiet -- which is what every phone in #25's capture did -- while a
 * fixed camera keeps calling home for as long as it is powered. Low on purpose:
 * this only lights an evidence flag, it does not claim anything.
 */
#define SURVEY_PERSISTENT_COUNT 3

/**
 * Score and sort a survey, best-first.
 *
 * Stable for equal scores (input order is preserved), so a list does not
 * reshuffle under the operator's cursor when two rows tie.
 *
 * Rows whose fingerprint is a known-generic scan skeleton score 0 and sort last,
 * however close and however persistent they are. A hash carried by every phone
 * on the street cannot distinguish anything, and letting one head the list is
 * how an operator ends up confirming a phone as a camera.
 *
 * @param rows       input rows (not modified).
 * @param count      how many.
 * @param out        caller's array, written best-first.
 * @param out_max    capacity of @p out.
 * @return           number of entries written: min(count, out_max).
 */
size_t survey_rank(const SurveyRankRow* rows, size_t count, SurveyRanked* out, size_t out_max);

/**
 * True when `mac` is locally administered (bit 1 of the first octet).
 *
 * Such an address is invented by the device, so no manufacturer stands behind it
 * and no OUI lookup can succeed. It is the reason MAC-prefix matching finds
 * nothing on a modern camera -- and, equally, on most modern phones, which is
 * why this is reported as context and never scored as evidence of surveillance.
 */
bool survey_mac_is_local(const uint8_t* mac);

#ifdef __cplusplus
}
#endif
