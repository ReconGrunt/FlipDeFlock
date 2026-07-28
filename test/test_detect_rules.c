// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
//
// Tests for the pure detection rules extracted from recon_app.c (R3): geotag
// hysteresis, the anti-stalking waypoint/span track, and the "following" AND-gate.
#include "detect_rules.h"
#include "test.h"

#include <math.h>
#include <stdio.h>

static bool nearf(float a, float b, float tol) {
    return fabsf(a - b) <= tol;
}

void suite_detect_rules(void) {
    printf("[detect_rules]\n");

    // --- detect_dist_m ------------------------------------------------------
    CHECK(nearf(detect_dist_m(0, 0, 0, 0), 0.0f, 0.01f));
    CHECK(nearf(detect_dist_m(0, 0, 0.001f, 0), 111.32f, 1.0f)); // ~111 m per mdeg lat
    CHECK(nearf(detect_dist_m(0, 0, 0, 0.001f), 111.32f, 1.0f)); // lon at the equator
    CHECK(nearf(detect_dist_m(60, 0, 60, 0.001f), 55.66f, 1.0f)); // lon at 60N (cos60 = 0.5)

    // --- flock_geotag_should_update ----------------------------------------
    CHECK(!flock_geotag_should_update(false, false, -40, -80)); // no fix -> never
    CHECK(flock_geotag_should_update(true, false, -70, 0)); // fix + not yet tagged
    CHECK(!flock_geotag_should_update(true, true, -50, -50)); // tagged, not stronger
    CHECK(!flock_geotag_should_update(true, true, -44, -50)); // exactly +6 -> not strictly >
    CHECK(flock_geotag_should_update(true, true, -43, -50)); // +7 -> refresh
    CHECK(flock_geotag_should_update(true, true, -40, -50)); // clearly stronger

    // --- ble_following_gate (AND of all four thresholds) --------------------
    CHECK(ble_following_gate(4, 90000, 3, 150.0f)); // exactly at every threshold
    CHECK(ble_following_gate(10, 120000, 5, 300.0f)); // comfortably past
    CHECK(!ble_following_gate(3, 90000, 3, 150.0f)); // too few scans
    CHECK(!ble_following_gate(4, 89999, 3, 150.0f)); // window too short
    CHECK(!ble_following_gate(4, 90000, 2, 150.0f)); // too few waypoints
    CHECK(!ble_following_gate(4, 90000, 3, 149.9f)); // span too small

    // --- ble_track_fold_fix -------------------------------------------------
    // Seed the first waypoint from a fresh (NAN) track.
    BleTrack t = {
        .first_lat = NAN,
        .first_lon = NAN,
        .last_wp_lat = NAN,
        .last_wp_lon = NAN,
        .waypoints = 0,
        .max_span_m = 0.0f};
    ble_track_fold_fix(&t, 0.0f, 0.0f);
    CHECK_INT_EQ(t.waypoints, 1);
    CHECK(nearf(t.last_wp_lat, 0.0f, 1e-6f));

    // A fix within WAYPOINT_GAP_M does NOT advance the waypoint.
    ble_track_fold_fix(&t, 0.0002f, 0.0f); // ~22 m
    CHECK_INT_EQ(t.waypoints, 1);

    // Created WITHOUT an origin (first_lat NAN): a far fix advances the waypoint
    // but the span can't grow (no track origin to measure from).
    ble_track_fold_fix(&t, 0.001f, 0.0f); // ~111 m from the last waypoint -> advance
    CHECK_INT_EQ(t.waypoints, 2);
    CHECK(nearf(t.max_span_m, 0.0f, 0.01f));

    // A real "following" track: origin known, three >=50 m hops build the
    // waypoint count and span up past the gate.
    BleTrack tr = {
        .first_lat = 0.0f,
        .first_lon = 0.0f,
        .last_wp_lat = 0.0f,
        .last_wp_lon = 0.0f,
        .waypoints = 1,
        .max_span_m = 0.0f};
    ble_track_fold_fix(&tr, 0.000600f, 0.0f); // ~67 m -> wp2, span ~67
    ble_track_fold_fix(&tr, 0.001200f, 0.0f); // ~67 m hop -> wp3, span ~134
    ble_track_fold_fix(&tr, 0.001800f, 0.0f); // ~67 m hop -> wp4, span ~200
    CHECK_INT_EQ(tr.waypoints, 4);
    CHECK(tr.max_span_m >= FOLLOW_MIN_SPAN_M);
    CHECK(ble_following_gate(4, 90000, tr.waypoints, tr.max_span_m)); // now "following"

    // --- flock_alert_should_fire (issue #1 alert gate) ----------------------
    // Confidence rungs: 0 None, 1 Possible, 2 Likely, 3 ProbeFp, 4 Confirmed.

    // A brand-new device (prev 0) at Likely or better fires; below that, never.
    CHECK(flock_alert_should_fire(0, 2, false, 1000, 0, false)); // Likely
    CHECK(flock_alert_should_fire(0, 4, false, 1000, 0, false)); // Confirmed
    CHECK(!flock_alert_should_fire(0, 1, false, 1000, 0, false)); // OUI-only "Possible"
    CHECK(!flock_alert_should_fire(0, 0, false, 1000, 0, false)); // None

    // The per-entry latch stops a camera re-alerting every time it's seen again.
    CHECK(!flock_alert_should_fire(0, 4, true, 1000, 0, false));
    CHECK(!flock_alert_should_fire(2, 4, true, 100000, 1000, true));

    // Possible -> Confirmed is a crossing and fires exactly once; a device that
    // already qualified does not re-fire when it climbs further.
    CHECK(flock_alert_should_fire(1, 4, false, 100000, 1000, true));
    CHECK(!flock_alert_should_fire(2, 4, false, 100000, 1000, true)); // already >= Likely
    CHECK(!flock_alert_should_fire(4, 4, false, 100000, 1000, true)); // no change at all

    // Cooldown: a second device inside ALERT_COOLDOWN_MS is suppressed, and the
    // same device is allowed once the window has passed.
    CHECK(!flock_alert_should_fire(0, 4, false, 1000 + ALERT_COOLDOWN_MS - 1, 1000, true));
    CHECK(flock_alert_should_fire(0, 4, false, 1000 + ALERT_COOLDOWN_MS, 1000, true));

    // The FIRST alert of a session must not be swallowed by the cooldown: with
    // last_alert_tick still 0 and a small tick, the elapsed test would otherwise
    // read as "an alert 12 ms ago". have_alerted_before is what prevents that.
    CHECK(flock_alert_should_fire(0, 4, false, 12, 0, false));
    CHECK(!flock_alert_should_fire(0, 4, false, 12, 0, true)); // genuinely 12 ms ago
}
