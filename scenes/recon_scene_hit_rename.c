// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// Give a detection your own name.
//
// The label NEVER overwrites the SSID. What the device broadcast and what the
// operator decided it is are two different facts, and collapsing them throws away
// the evidence: "RANCHO" is what a probe request asked for, "solar inverter, not a
// camera" is what the operator worked out afterwards. The list shows the label
// when there is one, the detail screen and every report still carry the observed
// name underneath.
#include "../recon_app_i.h"

static void recon_scene_hit_rename_done(void* context) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void recon_scene_hit_rename_on_enter(void* context) {
    ReconApp* app = context;

    // Seed with the existing label so an edit is an edit, not a retype. Falls
    // back to the observed SSID as a starting point, which is usually most of
    // what the operator wants to say anyway.
    app->rename_buf[0] = '\0';
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->hit_menu_idx >= 0 && app->hit_menu_idx < (int)app->flock_count) {
        FlockEntry* e = &app->flock[app->hit_menu_idx];
        const char* seed = e->label[0] ? e->label : e->ssid;
        snprintf(app->rename_buf, sizeof(app->rename_buf), "%s", seed);
    }
    furi_mutex_release(app->mutex);

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name this hit");
    text_input_set_result_callback(
        app->text_input,
        recon_scene_hit_rename_done,
        app,
        app->rename_buf,
        sizeof(app->rename_buf),
        // false = keep what we seeded, so the operator can correct a name
        // instead of clearing the field every time.
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewTextInput);
}

bool recon_scene_hit_rename_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->hit_menu_idx >= 0 && app->hit_menu_idx < (int)app->flock_count) {
        FlockEntry* e = &app->flock[app->hit_menu_idx];
        snprintf(e->label, sizeof(e->label), "%s", app->rename_buf);
    }
    furi_mutex_release(app->mutex);
    recon_hits_save(app);

    // Back past the action menu to the list: the operator asked to name one
    // thing, and making them dismiss a menu they are finished with is friction.
    scene_manager_previous_scene(app->scene_manager);
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void recon_scene_hit_rename_on_exit(void* context) {
    ReconApp* app = context;
    text_input_reset(app->text_input);
}
