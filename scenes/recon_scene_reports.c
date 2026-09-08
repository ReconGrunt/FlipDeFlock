// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "../recon_app_i.h"
#include "../helpers/recon_report.h"
#include "../helpers/sig_db.h"

typedef enum {
    ReportItemSave,
    ReportItemSaveAll,
    ReportItemSaveRaw,
    ReportItemFalsePos,
    ReportItemClear,
    ReportItemClearSaved,
    ReportItemForgetLearned,
} ReportItem;

static void recon_scene_reports_submenu_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void recon_scene_reports_popup_cb(void* context) {
    ReconApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

static void recon_scene_reports_build_menu(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    int marked = 0;
    int archived = 0;
    for(size_t i = 0; i < app->flock_count; i++) {
        if(app->flock[i].marked) marked++;
        if(app->flock[i].archived) archived++;
    }
    furi_mutex_release(app->mutex);

    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    snprintf(app->text_store, RECON_TEXT_STORE, "Reports (%d marked)", marked);
    submenu_set_header(submenu, app->text_store);
    // REDACTION IS THE DEFAULT POSITION, and it is expressed as separate menu
    // items rather than a setting. A setting is decided once, months before the
    // export that matters, and then forgotten; the label under the cursor is read
    // every time. The one item that writes an unredacted file says so in its own
    // name and sorts last, so it cannot be reached by muscle memory aimed at the
    // first entry.
    submenu_add_item(
        submenu, "Export Marked (Redacted)", ReportItemSave, recon_scene_reports_submenu_cb, app);
    submenu_add_item(
        submenu, "Export All (Redacted)", ReportItemSaveAll, recon_scene_reports_submenu_cb, app);
    submenu_add_item(
        submenu,
        "Export All (RAW - private)",
        ReportItemSaveRaw,
        recon_scene_reports_submenu_cb,
        app);
    // Redacted export for reporting a WRONG detection. Separate item rather than
    // an option on the one above, because the two files have opposite jobs: that
    // report is evidence about cameras and carries coordinates, this one is
    // evidence about the detector and must not.
    submenu_add_item(
        submenu, "False Positive Report", ReportItemFalsePos, recon_scene_reports_submenu_cb, app);
    submenu_add_item(
        submenu, "Clear All Marks", ReportItemClear, recon_scene_reports_submenu_cb, app);
    // Erase the persisted hit log. Offered whenever the setting is on OR stored
    // entries are still loaded, so it stays reachable to undo a past session.
    if(app->settings.save_hits || archived > 0) {
        submenu_add_item(
            submenu, "Clear Saved Hits", ReportItemClearSaved, recon_scene_reports_submenu_cb, app);
    }
    // Only when there is something to forget, and it says HOW MANY -- a learned
    // signature is otherwise completely invisible: it lives in a file, changes
    // scoring on a later drive, and nothing on screen would ever mention it.
    size_t learned = sig_db_learned_count(app->storage);
    if(learned > 0) {
        snprintf(app->text_store, RECON_TEXT_STORE, "Forget Learned (%u)", (unsigned)learned);
        submenu_add_item(
            submenu, app->text_store, ReportItemForgetLearned, recon_scene_reports_submenu_cb, app);
    }
}

void recon_scene_reports_on_enter(void* context) {
    ReconApp* app = context;
    recon_scene_reports_build_menu(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

static void recon_scene_reports_show_popup(ReconApp* app, const char* header, const char* text) {
    Popup* popup = app->popup;
    popup_reset(popup);
    popup_set_header(popup, header, 64, 10, AlignCenter, AlignTop);
    popup_set_text(popup, text, 64, 30, AlignCenter, AlignTop);
    popup_set_context(popup, app);
    popup_set_callback(popup, recon_scene_reports_popup_cb);
    popup_set_timeout(popup, 2500);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewPopup);
}

bool recon_scene_reports_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ReportItemSave || event.event == ReportItemSaveAll ||
           event.event == ReportItemSaveRaw) {
            uint8_t flags = 0;
            if(event.event != ReportItemSaveRaw) flags |= ReconExportRedact;
            if(event.event != ReportItemSave) flags |= ReconExportAll;
            char path[128] = {0};
            bool ok = recon_report_save_flock(app, path, sizeof(path), flags);
            if(app->settings.sound) {
                notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
            }
            // The popup names the posture as well. All three items write into the
            // same folder and only the filename tells them apart, so the last
            // thing on screen after a save should be WHICH kind was written --
            // not a generic "Report Saved" that reads identically for all three.
            const char* fail_text =
                (event.event == ReportItemSave) ? "Mark detections first" : "No detections yet";
            recon_scene_reports_show_popup(
                app,
                ok ? ((flags & ReconExportRedact) ? "Redacted Report" : "RAW Report") :
                     "Nothing to Save",
                ok ? ((flags & ReconExportRedact) ? "OUI only, no times,\nno other SSIDs" :
                                                    "Full MACs + SSIDs.\nDo not share.") :
                     fail_text);
            consumed = true;
        } else if(event.event == ReportItemFalsePos) {
            char path[128] = {0};
            bool ok = recon_report_save_fp(app, path, sizeof(path));
            if(app->settings.sound) {
                notification_message(app->notifications, ok ? &sequence_success : &sequence_error);
            }
            recon_scene_reports_show_popup(
                app,
                ok ? "FP Report Saved" : "Nothing to Save",
                ok ? "Redacted: no GPS,\nno full MACs" : "No detections yet");
            consumed = true;
        } else if(event.event == ReportItemClear) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(size_t i = 0; i < app->flock_count; i++) {
                app->flock[i].marked = false;
            }
            furi_mutex_release(app->mutex);
            recon_scene_reports_build_menu(app);
            recon_scene_reports_show_popup(app, "Marks Cleared", "");
            consumed = true;
        } else if(event.event == ReportItemForgetLearned) {
            bool gone = sig_db_forget_learned(app->storage);
            recon_scene_reports_build_menu(app);
            recon_scene_reports_show_popup(
                app,
                gone ? "Learned Cleared" : "Nothing Learned",
                gone ? "Takes effect on\nnext app start" : "");
            consumed = true;
        } else if(event.event == ReportItemClearSaved) {
            recon_hits_clear(app); // deletes hits.csv AND drops the restored entries
            recon_scene_reports_build_menu(app);
            recon_scene_reports_show_popup(app, "Saved Hits Cleared", "hits.csv deleted");
            consumed = true;
        }
    }
    return consumed;
}

void recon_scene_reports_on_exit(void* context) {
    ReconApp* app = context;
    popup_reset(app->popup);
    submenu_reset(app->submenu);
}
