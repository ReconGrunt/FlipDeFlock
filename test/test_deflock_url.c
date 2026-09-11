// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// The Share to DeFlock deep link -- issue #25.
//
// The bug it pins: every QR up to v0.95 pointed at deflock.org, which is the
// marketing landing page. It has no map, ignores lat/lng entirely, and dropped
// the reporter on "Welcome to DeFlock" after he drove to a camera. The fix is
// two facts (the maps. subdomain, and a zoom that is not optional) plus a
// buffer wide enough for the worst-case position, and none of the three were
// checkable while the string lived inside a scene.
//
// COORDINATES HERE ARE THE PROJECT'S PUBLISHED BENCH POINT (lower Manhattan),
// the same obviously-fake place tools/flock_emitter transmits. No real reported
// sighting and no maintainer position goes in a file that ships.
#include "deflock_url.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

void suite_deflock_url(void) {
    printf("[deflock_url]\n");

    char u[DEFLOCK_URL_LEN];

    // --- the exact string ------------------------------------------------------
    size_t n = deflock_map_url(u, sizeof(u), 40.712799, -74.006004);
    CHECK_STR_EQ(u, "https://maps.deflock.org/?lat=40.712799&lng=-74.006004&zoom=18");
    CHECK_INT_EQ((int)n, (int)strlen(u));
    CHECK_INT_EQ((int)n, 62);

    // --- the subdomain is the whole fix ---------------------------------------
    // Asserted as a prefix, not a substring: "https://deflock.org/?lat=..." also
    // contains "deflock.org", so a substring check would pass the original bug.
    CHECK(strncmp(u, "https://maps.deflock.org/?", 26) == 0);

    // --- zoom is not optional --------------------------------------------------
    // Without it the map opens at zoom 4 and the camera is an invisible speck.
    CHECK_STR_CONTAINS(u, "zoom=18");

    // --- six decimal places, i.e. ~0.1 m ---------------------------------------
    // Enough to tell one pole from the next one down the street.
    deflock_map_url(u, sizeof(u), 1.0, 2.0);
    CHECK_STR_EQ(u, "https://maps.deflock.org/?lat=1.000000&lng=2.000000&zoom=18");

    // --- DEFLOCK_URL_LEN really does cover the worst case -----------------------
    // Southern hemisphere, three-digit longitude: both signs present and the
    // longest integer part either field can take. 64 characters plus the NUL is
    // 65, which is why the scene's old char[64] would have encoded a truncated --
    // and therefore dead -- link into the QR with nothing on screen to say so.
    n = deflock_map_url(u, sizeof(u), -33.868820, -151.209290);
    CHECK_STR_EQ(u, "https://maps.deflock.org/?lat=-33.868820&lng=-151.209290&zoom=18");
    CHECK_INT_EQ((int)n, 64);
    CHECK(n + 1 <= DEFLOCK_URL_LEN);

    // --- truncation is reported, never returned --------------------------------
    // A half-written URL scans to a dead link, which is worse than no QR: the
    // scanner gets a page, just not the right one. char[64] is exactly the old
    // buffer, and exactly one byte short for the position above.
    char small[64];
    memset(small, 'x', sizeof(small));
    n = deflock_map_url(small, sizeof(small), -33.868820, -151.209290);
    CHECK_INT_EQ((int)n, 0);
    CHECK_INT_EQ((int)strlen(small), 0);

    // Degenerate sizes must not write past the end or crash.
    CHECK_INT_EQ((int)deflock_map_url(NULL, 16, 1.0, 2.0), 0);
    char one[1] = {'x'};
    CHECK_INT_EQ((int)deflock_map_url(one, sizeof(one), 1.0, 2.0), 0);
    CHECK_INT_EQ((int)one[0], 0);
}
