// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
//
// Confidence-scoring truth tables for flock_db. Locks in B6 (strict
// "Flock-XXXXXX" provisioning-AP anchoring) and the precision-first contracts:
// OUI-only never confirms, user IE-fingerprints stay UNVERIFIED.
#include "flock_db.h"
#include "test.h"

#include <stdio.h>

void suite_flock_db(void) {
    printf("[flock_db]\n");

    // --- flock_ssid_confidence ---------------------------------------------
    CHECK_INT_EQ(flock_ssid_confidence(NULL), FlockConfidenceNone);
    CHECK_INT_EQ(flock_ssid_confidence(""), FlockConfidenceNone);

    // Exactly "Flock-" + 6 hex -> Confirmed (the provisioning AP), any case.
    CHECK_INT_EQ(flock_ssid_confidence("Flock-A1B2C3"), FlockConfidenceConfirmed);
    CHECK_INT_EQ(flock_ssid_confidence("flock-a1b2c3"), FlockConfidenceConfirmed);
    CHECK_INT_EQ(flock_ssid_confidence("Flock-000000"), FlockConfidenceConfirmed);

    // B6 regression: benign names that merely CONTAIN "flock-" must NOT confirm.
    // They fall through to "Likely" (still contain the "flock" substring).
    CHECK_INT_EQ(flock_ssid_confidence("Flock-Guest"), FlockConfidenceLikely);
    CHECK_INT_EQ(flock_ssid_confidence("Flock Freight WiFi"), FlockConfidenceLikely);
    CHECK_INT_EQ(flock_ssid_confidence("Flock-12345"), FlockConfidenceLikely); // 5 hex: too short
    CHECK_INT_EQ(flock_ssid_confidence("Flock-1234567"), FlockConfidenceLikely); // 7: too long
    CHECK_INT_EQ(flock_ssid_confidence("Flock-GHIJKL"), FlockConfidenceLikely); // non-hex

    // Built-in service SSID + weaker substrings.
    CHECK_INT_EQ(flock_ssid_confidence("test_flck"), FlockConfidenceConfirmed);
    CHECK_INT_EQ(flock_ssid_confidence("MyFlockNet"), FlockConfidenceLikely);
    CHECK_INT_EQ(flock_ssid_confidence("somethingflck"), FlockConfidenceLikely);
    CHECK_INT_EQ(flock_ssid_confidence("Starbucks"), FlockConfidenceNone);

    // --- flock_oui_match ----------------------------------------------------
    const uint8_t known[6] = {0xb4, 0x1e, 0x52, 0x00, 0x00, 0x01}; // Flock's own OUI
    const uint8_t unknown[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    CHECK(flock_oui_match(known));
    CHECK(!flock_oui_match(unknown));
    CHECK(!flock_oui_match(NULL));

    // --- extra OUIs (user signatures merged OVER the built-ins) --------------
    static const uint8_t extra[][3] = {{0x11, 0x22, 0x33}};
    FlockDbExtras ex_oui = {.ouis = extra, .oui_count = 1};
    flock_db_set_extras(&ex_oui);
    CHECK(flock_oui_match(unknown)); // now matches via the extra
    flock_db_set_extras(NULL);
    CHECK(!flock_oui_match(unknown)); // cleared -> built-ins only (fail-safe)

    // --- extra SSID patterns (needles are lower-case per the contract) -------
    static const char* const conf[] = {"acme-cam"};
    static const char* const like[] = {"widgetcorp"};
    FlockDbExtras ex_ssid = {
        .ssid_confirmed = conf,
        .ssid_confirmed_count = 1,
        .ssid_likely = like,
        .ssid_likely_count = 1};
    flock_db_set_extras(&ex_ssid);
    CHECK_INT_EQ(flock_ssid_confidence("ACME-CAM-07"), FlockConfidenceConfirmed);
    CHECK_INT_EQ(flock_ssid_confidence("WidgetCorp Guest"), FlockConfidenceLikely);
    flock_db_set_extras(NULL);
    CHECK_INT_EQ(flock_ssid_confidence("ACME-CAM-07"), FlockConfidenceNone);

    // --- IE-fingerprint match + UNVERIFIED user cap contract ----------------
    CHECK_INT_EQ(flock_ie_fp_match(0), FlockIeFpNone); // 0 = "no fingerprint"
    CHECK_INT_EQ(flock_ie_fp_match(0xdeadbeef), FlockIeFpNone); // built-in table ships empty
    static const uint32_t ufps[] = {0xdeadbeef};
    FlockDbExtras ex_fp = {.ie_fps = ufps, .ie_fp_count = 1};
    flock_db_set_extras(&ex_fp);
    CHECK_INT_EQ(flock_ie_fp_match(0xdeadbeef), FlockIeFpUser); // user match, never "builtin"
    CHECK_INT_EQ(flock_ie_fp_match(0x12345678), FlockIeFpNone);
    flock_db_set_extras(NULL);
    CHECK_INT_EQ(flock_ie_fp_match(0xdeadbeef), FlockIeFpNone);

    // --- flock_score contract ----------------------------------------------
    const uint8_t nomac[6] = {0, 0, 0, 0, 0, 0};
    CHECK_INT_EQ(flock_score(known, "Flock-A1B2C3", false), FlockConfidenceConfirmed); // ssid wins
    CHECK_INT_EQ(flock_score(known, NULL, true), FlockConfidenceLikely); // OUI + probe
    CHECK_INT_EQ(flock_score(known, NULL, false), FlockConfidencePossible); // OUI only
    CHECK_INT_EQ(flock_score(nomac, NULL, true), FlockConfidenceNone); // nothing matched

    // --- confidence label strings ------------------------------------------
    CHECK_STR_EQ(flock_confidence_str(FlockConfidenceConfirmed), "CONFIRMED");
    CHECK_STR_EQ(flock_confidence_str(FlockConfidenceProbeFp), "Class?");
    CHECK_STR_EQ(flock_confidence_str(FlockConfidenceLikely), "Likely");
    CHECK_STR_EQ(flock_confidence_str(FlockConfidencePossible), "Possible");
    CHECK_STR_EQ(flock_confidence_str(FlockConfidenceNone), "-");
}
