// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// Name, version, and the people who built it. Nothing else.
//
// THIS SCREEN USED TO BE ~3.6 KB OF LITERAL TEXT, and the Help screen was
// another ~3.6 KB. On a FAP every byte of .rodata is loaded into RAM alongside
// .text and .bss, so those two strings alone were costing more than 7 KB of the
// heap for the whole app run. That mattered: with the app running, the largest
// contiguous block left was ~25 KB, and the ESP flasher plugin needs ~23 KB in
// one piece, so a firmware backup ran the device out of memory and crashed it.
// Documentation belongs in README.md and docs/, which cost nothing at runtime.
//
// Bots are deliberately not credited. Dependabot is not a contributor.
#include "../recon_app_i.h"

// The author is named under the title; CONTRIBUTORS is for people who
// contributed TO the project, which is not the same list.
#define RECON_ABOUT_TEXT              \
    "FlipDeFlock " RECON_VERSION "\n" \
    "Made by ReconGrunt\n \n"         \
    "Find the cameras that\n"         \
    "are watching you.\n \n"          \
    "CONTRIBUTORS\n"                  \
    "h00die\n"                        \
    "nickk02\n \n"                    \
    "GPL-3.0-or-later\n"              \
    "Free forever."

void recon_scene_about_on_enter(void* context) {
    ReconApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);
    // x=2/y=1: at 0,0 the first baseline lands on row 0 and the top pixel row of
    // every glyph in the title is cut off against the bezel.
    widget_add_text_scroll_element(widget, 2, 1, 124, 63, RECON_ABOUT_TEXT);
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
