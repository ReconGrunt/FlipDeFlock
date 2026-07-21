// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
//
// WiFi security-grading tests. Locks in B15: an unrecognised-but-modern auth
// mode grades Ok, NOT Info (Info is a higher enum value, so it would sort a
// strong network as *worse* than WPA2/WPA3).
#include "wifi_audit.h"
#include "test.h"

#include <core/string.h>
#include <stdio.h>

void suite_wifi_audit(void) {
    printf("[wifi_audit]\n");
    FuriString* r = furi_string_alloc();

    // Known auth modes -> documented grades (reasons unused -> NULL).
    CHECK_INT_EQ(wifi_audit_grade(0 /*OPEN*/, 0, false, "x", NULL), WifiGradeCritical);
    CHECK_INT_EQ(wifi_audit_grade(1 /*WEP*/, 0, false, "x", NULL), WifiGradeCritical);
    CHECK_INT_EQ(wifi_audit_grade(2 /*WPA1*/, 0, false, "x", NULL), WifiGradeWeak);
    CHECK_INT_EQ(wifi_audit_grade(3 /*WPA2*/, 0, false, "x", NULL), WifiGradeOk);
    CHECK_INT_EQ(wifi_audit_grade(5 /*WPA2-Ent*/, 0, false, "x", NULL), WifiGradeStrong);
    CHECK_INT_EQ(wifi_audit_grade(6 /*WPA3*/, 0, false, "x", NULL), WifiGradeStrong);

    // B15 regression: an unknown-but-modern auth mode grades Ok, not Info.
    furi_string_reset(r);
    WifiGrade g = wifi_audit_grade(10 /*e.g. WPA3-Ent-192*/, 0, false, "x", r);
    CHECK_INT_EQ(g, WifiGradeOk);
    CHECK(g != WifiGradeInfo);
    CHECK(WifiGradeOk < WifiGradeInfo); // the ordering that makes the bug matter
    CHECK_STR_CONTAINS(furi_string_get_cstr(r), "Modern auth");

    // WPS and TKIP each downgrade to at least WEAK.
    CHECK_INT_EQ(wifi_audit_grade(3 /*WPA2*/, 0, true /*WPS*/, "x", NULL), WifiGradeWeak);
    CHECK_INT_EQ(wifi_audit_grade(6 /*WPA3*/, 3 /*TKIP*/, false, "x", NULL), WifiGradeWeak);

    // Hidden SSID adds a note but does not change the grade.
    furi_string_reset(r);
    CHECK_INT_EQ(wifi_audit_grade(3, 0, false, NULL, r), WifiGradeOk);
    CHECK_STR_CONTAINS(furi_string_get_cstr(r), "Hidden");

    // Labels.
    CHECK_STR_EQ(wifi_grade_str(WifiGradeCritical), "CRIT");
    CHECK_STR_EQ(wifi_grade_str(WifiGradeOk), "OK");
    CHECK_STR_EQ(wifi_grade_str(WifiGradeStrong), "STRONG");
    CHECK_STR_EQ(wifi_grade_str(WifiGradeInfo), "INFO");
    CHECK_STR_EQ(wifi_auth_str(0), "Open");
    CHECK_STR_EQ(wifi_auth_str(3), "WPA2");
    CHECK_STR_EQ(wifi_auth_str(10), "?");

    furi_string_free(r);
}
