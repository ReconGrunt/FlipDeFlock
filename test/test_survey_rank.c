// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// Survey ranking. The headline case is issue #25's real capture: the ranker has
// to pick out the same row a human picked out by hand, from the same data.
#include "survey_rank.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

// Convenience: build a row without repeating the brace soup.
static SurveyRankRow
    mk(uint8_t b0,
       uint8_t b1,
       uint8_t b2,
       uint8_t b3,
       uint8_t b4,
       uint8_t b5,
       int8_t rssi,
       uint8_t ch,
       uint32_t fp,
       uint16_t count) {
    SurveyRankRow r;
    memset(&r, 0, sizeof(r));
    r.mac[0] = b0;
    r.mac[1] = b1;
    r.mac[2] = b2;
    r.mac[3] = b3;
    r.mac[4] = b4;
    r.mac[5] = b5;
    r.rssi = rssi;
    r.channel = ch;
    r.fp = fp;
    r.count = count;
    return r;
}

void suite_survey_rank(void) {
    printf("[survey_rank]\n");

    // --- locally administered bit -------------------------------------------
    // Bit 1 of the first octet. This is why an OUI table finds nothing on a
    // modern camera, and it is reported as context, never scored.
    const uint8_t local[6] = {0x06, 0xFC, 0xCB, 0x3A, 0xF8, 0x9E};
    const uint8_t global[6] = {0xF8, 0xD2, 0xAC, 0xC5, 0x97, 0xAC};
    CHECK(survey_mac_is_local(local));
    CHECK(!survey_mac_is_local(global));
    CHECK(!survey_mac_is_local(NULL));
    // 0x7E and 0x7A both have bit 1 set; 0x04 does not.
    const uint8_t seven_e[6] = {0x7E, 0x68, 0xF0, 0x04, 0xF6, 0x6B};
    const uint8_t seven_a[6] = {0x7A, 0xB2, 0x1B, 0x2C, 0xF2, 0xAD};
    const uint8_t four[6] = {0x04, 0xA8, 0x5A, 0x00, 0x00, 0x01};
    CHECK(survey_mac_is_local(seven_e));
    CHECK(survey_mac_is_local(seven_a));
    CHECK(!survey_mac_is_local(four));

    // --- degenerate inputs ---------------------------------------------------
    SurveyRanked out[16];
    SurveyRankRow one = mk(0x06, 0, 0, 0, 0, 1, -40, 6, 0xaaaaaaaau, 5);
    CHECK_INT_EQ((int)survey_rank(NULL, 1, out, 16), 0);
    CHECK_INT_EQ((int)survey_rank(&one, 0, out, 16), 0);
    CHECK_INT_EQ((int)survey_rank(&one, 1, NULL, 16), 0);
    CHECK_INT_EQ((int)survey_rank(&one, 1, out, 0), 0);

    // --- ISSUE #25, the real capture ----------------------------------------
    // Read straight from wiilover22's survey.csv. The camera he had physically
    // parked next to, ~50ft away, is 06:FC:CB:3A:F8:9E: -26 dBm and 10 probes
    // while nothing else in the file managed more than 2. Four of the 7E:68:F0
    // rows are one device rotating its address, all carrying 96fcd1b2 -- the
    // stock ESP32 scan skeleton, which is on the generic denylist and must sink
    // despite being the most "interesting looking" cluster in the file.
    SurveyRankRow field[7] = {
        mk(0x06, 0xFC, 0xCB, 0x3A, 0xF8, 0x9E, -26, 6, 0x89c3debfu, 10), // the camera
        mk(0x7E, 0x68, 0xF0, 0x04, 0xF6, 0x6B, -50, 1, 0x96fcd1b2u, 2), // generic
        mk(0x7E, 0x68, 0xF0, 0x8E, 0x5A, 0x48, -55, 6, 0x96fcd1b2u, 1), // generic
        mk(0x7E, 0x68, 0xF0, 0xB0, 0xCE, 0xFB, -60, 11, 0x96fcd1b2u, 1), // generic
        mk(0x7E, 0x68, 0xF0, 0x80, 0x2E, 0x5C, -62, 6, 0x96fcd1b2u, 1), // generic
        mk(0x7A, 0xB2, 0x1B, 0x2C, 0xF2, 0xAD, -37, 8, 0xba9fafa0u, 2), // 2nd camera?
        mk(0xF8, 0xD2, 0xAC, 0xC5, 0x97, 0xAC, -87, 6, 0xc4e51f77u, 1), // Vantiva, far
    };
    size_t n = survey_rank(field, 7, out, 16);
    CHECK_INT_EQ((int)n, 7);

    // The camera ranks first. This is the whole feature.
    CHECK_INT_EQ((int)out[0].index, 0);
    CHECK(out[0].score > out[1].score);
    CHECK(out[0].evidence & SurveyEvidenceLocalAdmin); // randomised, no OUI to match
    CHECK(out[0].evidence & SurveyEvidencePersistent); // 10 probes, not a burst
    CHECK(out[0].evidence & SurveyEvidenceClose); // -26 dBm
    CHECK(!(out[0].evidence & SurveyEvidenceGeneric));
    // One sighting, one address: it has NOT been seen rotating, and claiming so
    // would invent evidence.
    CHECK(!(out[0].evidence & SurveyEvidenceRotating));

    // The weaker second-camera candidate comes next: close, but a drive-by, so
    // only 2 probes and no persistence flag.
    CHECK_INT_EQ((int)out[1].index, 5);
    CHECK(out[1].evidence & SurveyEvidenceClose);
    CHECK(!(out[1].evidence & SurveyEvidencePersistent));

    // Every generic-skeleton row sinks to the bottom at score 0, including the
    // four-address rotating cluster that looks the most compelling by eye.
    for(size_t i = 3; i < 7; i++) {
        CHECK(out[i].evidence & SurveyEvidenceGeneric);
        CHECK_INT_EQ(out[i].score, 0);
    }
    // ...and the distant neighbour's set-top box, on a real vendor OUI, still
    // outranks them: it is at least a device we can name.
    CHECK_INT_EQ((int)out[2].index, 6);
    CHECK(!(out[2].evidence & SurveyEvidenceLocalAdmin));

    // --- generic rows do not set the persistence scale ------------------------
    // A chatty phone running a stock scan must not flatten the real rows. Here
    // the generic row has 100 probes; the camera's 10 should still score full
    // persistence because the generic row is excluded from the scale.
    SurveyRankRow skew[2] = {
        mk(0x06, 0, 0, 0, 0, 1, -30, 6, 0x11111111u, 10),
        mk(0x02, 0, 0, 0, 0, 2, -30, 6, 0x96fcd1b2u, 100),
    };
    n = survey_rank(skew, 2, out, 16);
    CHECK_INT_EQ((int)n, 2);
    CHECK_INT_EQ((int)out[0].index, 0);
    CHECK(out[0].score >= 80); // full persistence + close
    CHECK_INT_EQ(out[1].score, 0);

    // --- rotation is scored only for non-generic fingerprints ----------------
    // Same fingerprint, two different addresses: that is the randomisation tell
    // a fingerprint exists to catch.
    SurveyRankRow rot[2] = {
        mk(0x06, 0, 0, 0, 0, 1, -70, 6, 0x33333333u, 4),
        mk(0x0A, 0, 0, 0, 0, 2, -70, 6, 0x33333333u, 4),
    };
    n = survey_rank(rot, 2, out, 16);
    CHECK_INT_EQ((int)n, 2);
    CHECK(out[0].evidence & SurveyEvidenceRotating);
    CHECK(out[1].evidence & SurveyEvidenceRotating);

    // The same MAC appearing twice is NOT rotation.
    SurveyRankRow same[2] = {
        mk(0x06, 0, 0, 0, 0, 1, -70, 6, 0x33333333u, 4),
        mk(0x06, 0, 0, 0, 0, 1, -70, 11, 0x33333333u, 4),
    };
    n = survey_rank(same, 2, out, 16);
    CHECK(!(out[0].evidence & SurveyEvidenceRotating));

    // A zero fingerprint is "none captured", not a shared one -- two rows with
    // no fingerprint are not evidence of one device rotating.
    SurveyRankRow nofp[2] = {
        mk(0x06, 0, 0, 0, 0, 1, -70, 6, 0, 4),
        mk(0x0A, 0, 0, 0, 0, 2, -70, 6, 0, 4),
    };
    n = survey_rank(nofp, 2, out, 16);
    CHECK(!(out[0].evidence & SurveyEvidenceRotating));
    CHECK(!(out[1].evidence & SurveyEvidenceRotating));

    // --- output cap is honoured ----------------------------------------------
    SurveyRanked small[2];
    n = survey_rank(field, 7, small, 2);
    CHECK_INT_EQ((int)n, 2);

    // --- signal floor ---------------------------------------------------------
    // At and below the floor, proximity contributes nothing; a lone row still
    // takes full persistence because it is the busiest thing present.
    SurveyRankRow faint[1] = {mk(0x06, 0, 0, 0, 0, 1, -95, 6, 0x44444444u, 1)};
    n = survey_rank(faint, 1, out, 16);
    CHECK_INT_EQ((int)n, 1);
    CHECK(!(out[0].evidence & SurveyEvidenceClose));
    CHECK(!(out[0].evidence & SurveyEvidencePersistent));
}
