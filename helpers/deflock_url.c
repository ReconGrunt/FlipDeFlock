// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "deflock_url.h"

#include <stdio.h>

size_t deflock_map_url(char* out, size_t n, double lat, double lon) {
    if(!out || n == 0) return 0;
    out[0] = '\0';
    int w = snprintf(out, n, "https://maps.deflock.org/?lat=%.6f&lng=%.6f&zoom=18", lat, lon);
    // snprintf returns what it WOULD have written: a truncated URL is a QR that
    // scans to a broken link, which is worse than no QR at all, so report 0 and
    // leave the caller with an empty string rather than a plausible wrong one.
    if(w < 0 || (size_t)w >= n) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w;
}
