// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "../recon_app_i.h"

#define RECON_ABOUT_TEXT              \
    "FlipDeFlock " RECON_VERSION "\n" \
    "Snoop onto them as\n"            \
    "they snoop onto us.\n \n"        \
    "Passive Flock / ALPR scan.\n"    \
    "Pairs your Flipper with an\n"    \
    "ESP32 to survey the radio\n"     \
    "for surveillance gear.\n"        \
    "FLOCK / ALPR DETECT\n"           \
    "Finds Flock Safety / ALPR\n"     \
    "cameras via an ESP32\n"          \
    "(Companion FW or Marauder).\n"   \
    "Matches OUIs, phone-home\n"      \
    "probes, SSID names & probe\n"    \
    "IE fingerprints. OUI-only\n"     \
    "hits are 'Possible' - verify\n"  \
    "by eye, never assume.\n"         \
    "Five classes kept apart, so\n"   \
    "one is never announced as\n"     \
    "another: ALPR cameras, ST:\n"    \
    "acoustic sensors, AX: body\n"    \
    "cams (Axon / Utility /\n"        \
    "Digital Ally), VG: vendor\n"     \
    "gear of unknown kind\n"          \
    "(Ubicquia, Motorola,\n"          \
    "Verkada, Genetec, Avigilon),\n"  \
    "DR: unmanned aircraft.\n \n"     \
    "DRONES (Remote ID)\n"            \
    "Decodes the ASTM F3411\n"        \
    "broadcast every drone in US\n"   \
    "airspace must transmit, over\n"  \
    "BLE and WiFi. Shows its\n"       \
    "serial, type, position and\n"    \
    "THE OPERATOR'S POSITION.\n"      \
    "Most police drone makers\n"      \
    "hold no MAC block at all, so\n"  \
    "this is the only way to see\n"   \
    "them.\n \n"                      \
    "AIR SURVEY\n"                    \
    "Everything probing nearby,\n"    \
    "matched or not, ranked so\n"     \
    "the most camera-shaped\n"        \
    "behaviour is on top. A\n"        \
    "modern camera randomises\n"      \
    "its MAC and matches no\n"        \
    "vendor table, so this is\n"      \
    "often the only place it\n"       \
    "shows up. '~' marks a\n"         \
    "randomised address, 'g' a\n"     \
    "common scan pattern shared\n"    \
    "with phones.\n"                  \
    "Not detections. Open a row,\n"   \
    "then 'I saw it' AFTER you\n"     \
    "have looked with your own\n"     \
    "eyes, to teach the app that\n"   \
    "fingerprint - it survives\n"     \
    "the MAC changing. Capped at\n"   \
    "'Class?', never Confirmed.\n"    \
    "'Pin addr' instead saves the\n"  \
    "whole address, for a camera\n"   \
    "whose random MAC never\n"        \
    "changes.\n"                      \
    "Written to survey.csv, and\n"    \
    "survey_log.csv keeps every\n"    \
    "scan so a whole drive is\n"      \
    "one file.\n \n"                  \
    "LOCATOR (Companion)\n"           \
    "Hunt a marked device by\n"       \
    "signal: a hot/cold meter\n"      \
    "that climbs as you get\n"        \
    "closer. Works without GPS.\n"    \
    "Also targets BLE trackers -\n"   \
    "AirTag, Tile, SmartTag,\n"       \
    "Find My, other Flippers -\n"     \
    "so you can walk down a tag\n"    \
    "planted on your car.\n"          \
    "No compass arrow (that needs\n"  \
    "a directional antenna).\n \n"    \
    "FLOCK MAP\n"                     \
    "On-device map of detected\n"     \
    "cameras around your GPS fix.\n"  \
    " \n"                             \
    "REPORTS\n"                       \
    "Export to Markdown, DeFlock\n"   \
    "GeoJSON and KML under\n"         \
    "apps_data/flipdeflock/\n"        \
    "reports.\n"                      \
    "REDACTED BY DEFAULT.\n"          \
    "MACs drop to their OUI,\n"       \
    "times and heading go, and\n"     \
    "any SSID that is not a\n"        \
    "Flock name shows as a\n"         \
    "shape. Camera coordinates\n"     \
    "are kept.\n"                     \
    "'RAW' writes everything and\n"   \
    "names the file _RAW. That\n"     \
    "one is for you, not for\n"       \
    "sharing.\n"                      \
    "Share-to-DeFlock shows a\n"      \
    "QR to submit from your\n"        \
    "phone (no network).\n \n"        \
    "SIGNATURES\n"                    \
    "Drop apps_data/flipdeflock/\n"   \
    "signatures.json to add OUIs,\n"  \
    "whole MACs, SSID & IE-fp\n"      \
    "signatures.\n"                   \
    "Offline and fail-safe. Your\n"   \
    "file is never modified.\n \n"    \
    "LEARNING\n"                      \
    "'Confirm: I saw it' on a hit\n"  \
    "you actually looked at saves\n"  \
    "its probe fingerprint to\n"      \
    "learned.txt, so the same\n"      \
    "unit is caught again after\n"    \
    "it randomises its MAC.\n"        \
    "Learned signatures score\n"      \
    "'Class?' only, never\n"          \
    "Confirmed, so a wrong tap\n"     \
    "costs a weak lead, not a\n"      \
    "false camera.\n"                 \
    "Reports > Forget Learned\n"      \
    "wipes them. Nothing is ever\n"   \
    "sent anywhere.\n \n"             \
    "WIRING\n"                        \
    "ESP32 on USART\n"                \
    "(pin13 TX / pin14 RX).\n"        \
    "GPS on LPUART (pin15/16)\n"      \
    "so both run together.\n \n"      \
    "BOARD MODE (Settings)\n"         \
    "Marauder: keep your board\n"     \
    "as-is, no flashing - Flock\n"    \
    "detect, GPS & reports.\n"        \
    "Companion: flash our FW\n"       \
    "(via 'ESP32 Firmware') to\n"     \
    "add Locator, BLE Flock\n"        \
    "detection & dual-band\n"         \
    "Flock scanning.\n \n"            \
    "THANKS\n"                        \
    "Thank you to @h00die for\n"      \
    "actively contributing to\n"      \
    "the project!\n \n"               \
    "SUPPORT\n"                       \
    "Free forever - donations\n"      \
    "never gate a feature.\n"         \
    "BTC + more: Support menu\n \n"   \
    "Companion FW + OUI data:\n"      \
    "see esp32_companion/ and\n"      \
    "deflock.org. ALPR passive;\n"    \
    "lawful authorized use only."

void recon_scene_about_on_enter(void* context) {
    ReconApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);
    FuriString* s = furi_string_alloc();
    furi_string_printf(
        s,
        "Mode: %s\n \n%s",
        app->settings.backend == EspBackendGeneric ? "Marauder (Flock)" : "Companion (all)",
        RECON_ABOUT_TEXT);
    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(s));
    furi_string_free(s);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

bool recon_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void recon_scene_about_on_exit(void* context) {
    ReconApp* app = context;
    widget_reset(app->widget);
}
