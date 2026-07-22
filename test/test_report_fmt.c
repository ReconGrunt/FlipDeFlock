// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
//
// Tests for the shared report field emitters (R8): MAC + coordinate formatting.
#include "report_fmt.h"
#include "test.h"

#include <math.h>
#include <stdio.h>

void suite_report_fmt(void) {
    printf("[report_fmt]\n");
    char out[24];

    // --- fmt_mac ------------------------------------------------------------
    const uint8_t mac[6] = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
    fmt_mac(out, sizeof(out), mac);
    CHECK_STR_EQ(out, "A1:B2:C3:D4:E5:F6");
    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    fmt_mac(out, sizeof(out), zero);
    CHECK_STR_EQ(out, "00:00:00:00:00:00");

    // --- fmt_coord (exactly-representable floats so %.6f is deterministic) ---
    fmt_coord(out, sizeof(out), NAN, "-");
    CHECK_STR_EQ(out, "-"); // no fix -> table-cell fallback
    fmt_coord(out, sizeof(out), NAN, "");
    CHECK_STR_EQ(out, ""); // no fix -> omitted-field fallback
    fmt_coord(out, sizeof(out), 48.5f, "-");
    CHECK_STR_EQ(out, "48.500000");
    fmt_coord(out, sizeof(out), -11.25f, "");
    CHECK_STR_EQ(out, "-11.250000");
}
