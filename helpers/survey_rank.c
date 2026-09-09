// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// Ranking for the probe survey. See survey_rank.h for why this exists and for
// the standing rule that nothing here is a detection.
#include "survey_rank.h"
#include "flock_db.h" // flock_ie_fp_is_generic

#include <string.h>

/*
 * Weights. They sum to 100 and are integers on purpose -- no floats on the way
 * to a 128x64 screen, and an integer score is reproducible in a test.
 *
 * Persistence carries the most because it is the only component that separates
 * a fixed installation from a passer-by. Proximity is worth less than it looks:
 * being close makes a thing worth checking, but the operator's own phone is
 * closer than any camera will ever be. Rotation is the smallest, because
 * "several addresses, one fingerprint" is necessary evidence and not sufficient
 * evidence -- a common WiFi stack does it too (see CANDIDATES.md).
 */
#define W_PERSIST 45
#define W_CLOSE   35
#define W_ROTATE  20

/** Weakest signal that scores anything at all, in dBm. */
#define SURVEY_FLOOR_DBM (-90)

bool survey_mac_is_local(const uint8_t* mac) {
    if(!mac) return false;
    return (mac[0] & 0x02) != 0;
}

/** Distinct MACs carrying `fp`, capped at 2 -- all a caller needs to know. */
static uint8_t fp_mac_spread(const SurveyRankRow* rows, size_t count, size_t self, uint32_t fp) {
    if(fp == 0) return 1;
    uint8_t spread = 1;
    for(size_t i = 0; i < count; i++) {
        if(i == self) continue;
        if(rows[i].fp != fp) continue;
        if(memcmp(rows[i].mac, rows[self].mac, 6) == 0) continue;
        spread = 2;
        break;
    }
    return spread;
}

size_t survey_rank(const SurveyRankRow* rows, size_t count, SurveyRanked* out, size_t out_max) {
    if(!rows || !out || !out_max || !count) return 0;

    // The busiest row in this capture sets the persistence scale. Ten probes was
    // the tell in #25 only because nothing else beat two; on a street where
    // everything probes fifty times it would mean nothing.
    uint16_t busiest = 0;
    for(size_t i = 0; i < count; i++) {
        // A generic skeleton must not set the bar for everyone else, or one
        // chatty phone flattens every real row's persistence score to nothing.
        if(flock_ie_fp_is_generic(rows[i].fp)) continue;
        if(rows[i].count > busiest) busiest = rows[i].count;
    }

    size_t n = 0;
    for(size_t i = 0; i < count && n < out_max; i++) {
        const SurveyRankRow* r = &rows[i];
        uint8_t ev = 0;
        uint32_t score = 0;

        if(survey_mac_is_local(r->mac)) ev |= SurveyEvidenceLocalAdmin;

        if(flock_ie_fp_is_generic(r->fp)) {
            // Bottom of the list, unconditionally. Everything else about the row
            // may look excellent; the fingerprint says it cannot tell us apart
            // from a phone, so it is the one thing not worth walking over to.
            ev |= SurveyEvidenceGeneric;
        } else {
            if(busiest > 0) {
                score += (uint32_t)r->count * W_PERSIST / busiest;
            }
            if(r->count >= SURVEY_PERSISTENT_COUNT) ev |= SurveyEvidencePersistent;

            if(r->rssi > SURVEY_FLOOR_DBM) {
                int32_t above = (int32_t)r->rssi - SURVEY_FLOOR_DBM; // 0..60ish
                int32_t span = SURVEY_CLOSE_DBM - SURVEY_FLOOR_DBM; // 35
                if(above > span) above = span; // -55 dBm and better all cap out
                score += (uint32_t)(above * W_CLOSE / span);
            }
            if(r->rssi >= SURVEY_CLOSE_DBM) ev |= SurveyEvidenceClose;

            if(fp_mac_spread(rows, count, i, r->fp) >= 2) {
                ev |= SurveyEvidenceRotating;
                score += W_ROTATE;
            }
        }

        if(score > 100) score = 100;
        out[n].index = i;
        out[n].score = (uint8_t)score;
        out[n].evidence = ev;
        n++;
    }

    // Insertion sort, descending, stable. n is at most a few dozen rows and this
    // runs on a menu open, so the simple thing is the right thing.
    for(size_t i = 1; i < n; i++) {
        SurveyRanked v = out[i];
        size_t j = i;
        while(j > 0 && out[j - 1].score < v.score) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = v;
    }
    return n;
}
