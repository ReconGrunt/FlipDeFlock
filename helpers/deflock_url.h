// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#pragma once

#include <stddef.h>

/**
 * Build the DeFlock map deep link that the Share to DeFlock QR encodes.
 *
 * IT LIVES HERE SO A TEST CAN SEE IT. This was three lines inside a scene, which
 * means the only way to check it was to scan the QR off a Flipper screen -- and
 * for four releases nobody did: every code pointed at deflock.org, the marketing
 * page, which carries no map and silently discards lat/lng. wiilover22 reported
 * it on issue #25 after driving to a camera and getting "Welcome to DeFlock".
 *
 * Two things the fix depends on, both asserted in test_deflock_url.c:
 *
 *  - THE HOST IS maps.deflock.org. The apex domain is not a map.
 *  - THE ZOOM IS NOT OPTIONAL. Without it the map opens at zoom 4, a whole
 *    country, with the camera an invisible speck.
 *
 * @param out  destination; needs DEFLOCK_URL_LEN bytes for the worst case.
 * @param n    size of @p out.
 * @param lat  latitude, degrees.
 * @param lon  longitude, degrees.
 * @return     characters written, excluding the NUL (0 if it did not fit).
 */
size_t deflock_map_url(char* out, size_t n, double lat, double lon);

/**
 * Bytes needed for the longest URL this can produce, NUL included.
 *
 * The worst case is a southern-hemisphere position at a three-digit longitude,
 * e.g. -33.868820 / -151.209290, which is exactly 64 characters plus the NUL.
 * The scene used to carry char[64] -- one byte short -- so such a camera would
 * have encoded a truncated, broken link into the QR without any warning.
 */
#define DEFLOCK_URL_LEN 72
