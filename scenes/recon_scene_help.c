// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
#include "../recon_app_i.h"

// On-device reference for every mark this app can put on a screen.
//
// WHY THIS EXISTS. The header learned to report faults precisely -- !PORT, !PIN,
// !FW, and before those a whole vocabulary of confidence letters and tags -- and
// that is worth nothing to someone who cannot find out what the mark means. A
// user hit !PORT on his own device and said, exactly: "I don't know what it means
// and have no way of finding out." Naming a fault is only half the job; the other
// half is saying what to do about it, somewhere reachable without a browser.
//
// Ordered fault-first on purpose. Someone opening this page is almost always
// looking at a mark they do not recognise, and it is usually the one that is
// stopping something from working.
#define RECON_HELP_TEXT             \
    "HELP & WARNINGS\n"             \
    "What every mark means.\n \n"   \
    "-- GPS BADGE --\n"             \
    "GPS 9  locked, 9 sats.\n"      \
    "GPS    on, still searching.\n" \
    " \n"                           \
    "!PORT  GPS and the ESP are\n"  \
    "on the SAME UART. They\n"      \
    "cannot share one. Fix:\n"      \
    "Settings > GPS Port, put\n"    \
    "GPS on LPUART 15/16 and\n"     \
    "the ESP on USART 13/14.\n"     \
    "Until then GPS can never\n"    \
    "get a fix.\n \n"               \
    "!PIN   the companion board\n"  \
    "REFUSED that pin. It is\n"     \
    "not a usable GPIO on that\n"   \
    "chip, or it carries the\n"     \
    "flash or the Flipper link.\n"  \
    "Fix: Settings > ESP GPS\n"     \
    "Pin, pick another. The\n"      \
    "board reports its own\n"       \
    "valid pins.\n \n"              \
    "!FW    the companion never\n"  \
    "answered at all. Its\n"        \
    "firmware has no GPS relay.\n"  \
    "Fix: reflash it from\n"        \
    "'ESP32 Firmware'.\n \n"        \
    "No GPS module wired to\n"      \
    "your board at all? Then\n"     \
    "none of these apply and\n"     \
    "GPS simply cannot work.\n"     \
    "Plenty of ESP32 boards\n"      \
    "have no GNSS chip.\n \n"       \
    "-- SCAN HEADER --\n"           \
    "(wifi)55/s  Wi-Fi frames\n"    \
    "per second, live. '--'\n"      \
    "means not measured yet.\n"     \
    "(bt)490  BLE adverts this\n"   \
    "session. 'b-' means no BLE\n"  \
    "scan has finished yet, as\n"   \
    "opposed to one running and\n"  \
    "hearing nothing.\n"            \
    "a2   alerts DELIVERED. If\n"   \
    "this climbs and you hear\n"    \
    "nothing, the app fired and\n"  \
    "the Flipper swallowed it:\n"   \
    "check Flipper Settings >\n"    \
    "Notifications, and Alert\n"    \
    "on hit here. Settings has\n"   \
    "a 'Test alert' to try it.\n"   \
    "!r1  the companion RESET.\n"   \
    "It drops detections when\n"    \
    "it does. Usually power.\n"     \
    "!d3  RX lines dropped.\n"      \
    "!DEAUTH  a deauth flood is\n"  \
    "active nearby.\n \n"           \
    "-- DETECTION ROWS --\n"        \
    "!  CONFIRMED, SSID matches\n"  \
    "a known Flock pattern.\n"      \
    "L  Likely.\n"                  \
    "F  IE-fingerprint class\n"     \
    "match, not a unique unit.\n"   \
    "p  Possible: OUI prefix\n"     \
    "ONLY. Expect false\n"          \
    "positives. Verify by eye.\n"   \
    "ST  SoundThinking acoustic\n"  \
    "sensor, not a camera.\n"       \
    "[hid]  beacons with no\n"      \
    "SSID. Shown, never scored.\n"  \
    "*  you marked it.\n"           \
    "The Wi-Fi or Bluetooth\n"      \
    "mark says which radio saw\n"   \
    "it. A time like '6h' means\n"  \
    "a STORED hit, not a live\n"    \
    "one -- no bars are drawn\n"    \
    "for it on purpose.\n \n"       \
    "-- COMMON FIXES --\n"          \
    "No detections at all:\n"       \
    "check Board Mode matches\n"    \
    "your firmware, and that\n"     \
    "the frame rate is moving.\n"   \
    "Few detections on a C5:\n"     \
    "Settings > Band. 'Both'\n"     \
    "sweeps 41 channels instead\n"  \
    "of 13, so each camera is\n"    \
    "revisited a third as\n"        \
    "often. Flock uplinks are\n"    \
    "2.4GHz.\n"                     \
    "'UART busy': something\n"      \
    "else holds the port, or\n"     \
    "GPS and ESP are on the\n"      \
    "same one.\n"                   \
    "Hits vanished: Net\n"          \
    "Guardian used to wipe\n"       \
    "them. Fixed in v0.54.\n"       \
    "Detections are INDICATORS,\n"  \
    "never proof."

void recon_scene_help_on_enter(void* context) {
    ReconApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_text_scroll_element(widget, 0, 0, 128, 64, RECON_HELP_TEXT);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

bool recon_scene_help_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void recon_scene_help_on_exit(void* context) {
    ReconApp* app = context;
    widget_reset(app->widget);
}
