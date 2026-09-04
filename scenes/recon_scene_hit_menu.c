// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// The hold-OK action menu for one detection.
//
// WHY IT EXISTS. Coming back from a drive with two rows tagged and no memory of
// why either was tagged is not a usable record. A single anonymous "marked" bit
// cannot say "I got out and looked at this one" or "this turned out to be the
// neighbour's solar inverter". Confirming and naming are the two things that turn
// a list of guesses into ground truth, and ground truth is what promotes a
// candidate fingerprint out of the candidate table.
//
// WHY HOLD-OK. Tap-OK already opens the detail screen and Left is already Delete,
// both of which the operator uses constantly while driving. A long press adds the
// slower deliberate actions without taking a key away from the fast ones, and
// without a trip back to the main menu.
#include "../recon_app_i.h"

typedef enum {
    HitMenuConfirm,
    HitMenuRename,
    HitMenuMark,
    HitMenuDelete,
} HitMenuItem;

static void recon_scene_hit_menu_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void recon_scene_hit_menu_on_enter(void* context) {
    ReconApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    // Read the current state under the lock so the labels describe THIS device
    // rather than whatever was selected when the menu was last opened.
    bool confirmed = false, marked = false;
    char name[FLOCK_STORE_LABEL_LEN + RECON_SSID_LEN];
    name[0] = '\0';
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->hit_menu_idx >= 0 && app->hit_menu_idx < (int)app->flock_count) {
        FlockEntry* e = &app->flock[app->hit_menu_idx];
        confirmed = e->confirmed;
        marked = e->marked;
        if(e->label[0]) {
            snprintf(name, sizeof(name), "%s", e->label);
        } else if(e->ssid[0]) {
            snprintf(name, sizeof(name), "%s", e->ssid);
        } else {
            snprintf(name, sizeof(name), "%02X:%02X:%02X", e->mac[3], e->mac[4], e->mac[5]);
        }
    }
    furi_mutex_release(app->mutex);

    submenu_set_header(submenu, name[0] ? name : "Hit");
    // The toggles say what the press will DO, not what the state currently is: a
    // menu item that reads "Confirmed" leaves you guessing whether pressing it
    // sets or clears.
    submenu_add_item(
        submenu,
        confirmed ? "Unconfirm (seen)" : "Confirm: I saw it",
        HitMenuConfirm,
        recon_scene_hit_menu_cb,
        app);
    submenu_add_item(submenu, "Rename", HitMenuRename, recon_scene_hit_menu_cb, app);
    submenu_add_item(
        submenu,
        marked ? "Unmark for report" : "Mark for report",
        HitMenuMark,
        recon_scene_hit_menu_cb,
        app);
    submenu_add_item(submenu, "Delete", HitMenuDelete, recon_scene_hit_menu_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_hit_menu_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == HitMenuRename) {
        scene_manager_next_scene(app->scene_manager, ReconSceneHitRename);
        return true;
    }

    bool deleted = false;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->hit_menu_idx >= 0 && app->hit_menu_idx < (int)app->flock_count) {
        FlockEntry* e = &app->flock[app->hit_menu_idx];
        if(event.event == HitMenuConfirm) {
            e->confirmed = !e->confirmed;
        } else if(event.event == HitMenuMark) {
            e->marked = !e->marked;
        } else if(event.event == HitMenuDelete) {
            size_t i = (size_t)app->hit_menu_idx;
            // Close the gap rather than leaving a hole: every other consumer
            // walks flock[0..flock_count) and a tombstone would render.
            for(size_t j = i; j + 1 < app->flock_count; j++) {
                app->flock[j] = app->flock[j + 1];
            }
            app->flock_count--;
            deleted = true;
        }
    }
    furi_mutex_release(app->mutex);

    // Straight to the card. The whole point of confirming is that it is the one
    // fact in the table that cannot be re-derived later, so it must not be the
    // thing a flat battery takes with it.
    if(deleted) {
        recon_hits_save_after_delete(app);
    } else {
        recon_hits_save(app);
    }
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void recon_scene_hit_menu_on_exit(void* context) {
    ReconApp* app = context;
    submenu_reset(app->submenu);
}
