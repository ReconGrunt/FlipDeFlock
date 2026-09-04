// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// Everything currently in the table, newest first, for reviewing a drive after
// the fact rather than while it is happening.
//
// The scan screen is the wrong place to do this. It is live, rows arrive while
// you are reading, and the operator is usually driving. This is the sit-down
// view: confirm what you actually went and looked at, name the ones you worked
// out, delete the junk. OK opens the same hold-OK action menu the list uses, so
// there is one place that edits a hit rather than two that can disagree.
#include "../recon_app_i.h"
#include "../helpers/report_fmt.h"

// Table indexes in display order, so a submenu position maps back to the right
// device. Same reason the scan list keeps its own order[]: the table is in
// first-seen order and must stay that way for the store and the locator.
#define SAVED_MAX RECON_FLOCK_MAX
static int g_order[SAVED_MAX];
static int g_count;

static void recon_scene_saved_hits_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void recon_scene_saved_hits_on_enter(void* context) {
    ReconApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    g_count = 0;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    int n = (int)app->flock_count;
    if(n > SAVED_MAX) n = SAVED_MAX;
    for(int i = 0; i < n; i++)
        g_order[i] = i;
    // Newest first, same key as the scan list: seen_epoch is wall clock and is
    // written on every live sighting as well as carried by restored entries, so
    // it is the only key that orders live and archived rows against each other.
    for(int i = 1; i < n; i++) {
        int v = g_order[i];
        int j = i - 1;
        while(j >= 0 && app->flock[v].seen_epoch > app->flock[g_order[j]].seen_epoch) {
            g_order[j + 1] = g_order[j];
            j--;
        }
        g_order[j + 1] = v;
    }
    g_count = n;

    char row[40];
    for(int i = 0; i < n; i++) {
        FlockEntry* e = &app->flock[g_order[i]];
        // A tick for confirmed and a star for marked, so the two states the
        // operator sets by hand are visible without opening anything.
        char flags[3];
        int f = 0;
        if(e->confirmed) flags[f++] = '+';
        if(e->marked) flags[f++] = '*';
        flags[f] = '\0';

        // The operator's own name wins when there is one -- that is the whole
        // point of having set it. The observed SSID is still on the detail
        // screen and in every report.
        if(e->label[0]) {
            snprintf(row, sizeof(row), "%s%s", flags, e->label);
        } else if(e->ssid[0]) {
            snprintf(row, sizeof(row), "%s%s", flags, e->ssid);
        } else {
            snprintf(row, sizeof(row), "%s%02X:%02X:%02X", flags, e->mac[3], e->mac[4], e->mac[5]);
        }
        submenu_add_item(submenu, row, (uint32_t)i, recon_scene_saved_hits_cb, app);
    }
    furi_mutex_release(app->mutex);

    if(n == 0) {
        submenu_set_header(submenu, "Saved Hits - none");
        submenu_add_item(submenu, "Nothing stored yet", 0, recon_scene_saved_hits_cb, app);
    } else {
        char hdr[32];
        snprintf(hdr, sizeof(hdr), "Saved Hits (%d)", n);
        submenu_set_header(submenu, hdr);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_saved_hits_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if((int)event.event >= g_count) return true; // the empty-state placeholder row

    // Hand the action menu a TABLE index, the same currency the scan list uses.
    app->hit_menu_idx = g_order[event.event];
    scene_manager_next_scene(app->scene_manager, ReconSceneHitMenu);
    return true;
}

void recon_scene_saved_hits_on_exit(void* context) {
    ReconApp* app = context;
    submenu_reset(app->submenu);
}
