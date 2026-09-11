// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "../recon_app_i.h"
#include "../helpers/scene_util.h"

#include <furi/core/memmgr_heap.h>

/**
 * Largest free block the file picker needs before it is worth trying.
 *
 * Measured on the bench: opening it costs about 8.5 KB and it fails outright
 * below roughly that. 12 KB leaves headroom for the dialog plus the path
 * strings, and is still well under a healthy ~24 KB after one browser use.
 */
#define FW_BROWSER_MIN_BLOCK 12288u

#include <dialogs/dialogs.h>

typedef enum {
    FwItemBackup,
    FwItemFlash,
} FwItem;

static void fw_submenu_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void recon_scene_firmware_on_enter(void* context) {
    ReconApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    // NAME THE BUILD THAT IS ACTUALLY ON THE BOARD, not the file it came from.
    // The companion reports it in the FLOCKCO banner (v0.88+); anything older has
    // no build identity at all, which is the whole reason this exists -- an .bin
    // filename on the SD card is chosen by hand, cannot be checked after
    // flashing, and has been wrong. "unknown" here means "pre-v0.88 firmware",
    // which is itself the useful answer.
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool linked = app->esp_connected;
    char build[12];
    snprintf(build, sizeof(build), "%s", app->esp_build);
    furi_mutex_release(app->mutex);
    // Kept SHORT deliberately. "ESP32 Firmware (no link)" is 24 characters and
    // ran off the right edge of the 128 px header, cut mid-word -- caught by
    // looking at the screen, not by the build.
    if(!linked) {
        snprintf(app->text_store, RECON_TEXT_STORE, "ESP32 FW: no link");
    } else if(build[0]) {
        snprintf(app->text_store, RECON_TEXT_STORE, "ESP32 FW: v%s", build);
    } else {
        // Not an error: every companion before v0.88 reported no build at all.
        snprintf(app->text_store, RECON_TEXT_STORE, "ESP32 FW: pre-0.88");
    }
    submenu_set_header(submenu, app->text_store);
    submenu_add_item(submenu, "Backup current FW -> SD", FwItemBackup, fw_submenu_cb, app);
    submenu_add_item(submenu, "Flash a .bin", FwItemFlash, fw_submenu_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_firmware_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FwItemBackup) {
        app->fw_op = 0;
        storage_common_mkdir(app->storage, RECON_APP_FOLDER);
        storage_common_mkdir(app->storage, RECON_APP_FOLDER "/firmware");
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        snprintf(
            app->fw_path,
            sizeof(app->fw_path),
            RECON_APP_FOLDER "/firmware/backup_%04u%02u%02u_%02u%02u%02u.bin",
            dt.year,
            dt.month,
            dt.day,
            dt.hour,
            dt.minute,
            dt.second);
        scene_manager_next_scene(app->scene_manager, ReconSceneFirmwareRun);
        return true;
    }

    if(event.event == FwItemFlash) {
        // SAY SO WHEN THERE IS NOT ENOUGH MEMORY TO OPEN THE PICKER.
        //
        // dialog_file_browser_show() returns false both when the operator
        // cancels and when it could not allocate, so a low-memory failure was
        // indistinguishable from a Back press: the button simply did nothing,
        // twenty times over, and the app looked wedged. Seen on the bench after a
        // long session -- the largest free block had fallen to 13,816 bytes.
        //
        // Fragmentation is SYSTEM-WIDE and survives restarting the app, so
        // "reopen FlipDeFlock" is not the fix and must not be the advice; only a
        // reboot returns the heap to one piece.
        if(memmgr_heap_get_max_free_block() < FW_BROWSER_MIN_BLOCK) {
            scene_show_companion_guard(
                app,
                "Not enough free memory\nto open the file picker.\n\nRestart your Flipper (not\njust this app) -- the heap\nonly un-fragments on a\nreboot.");
            return true;
        }
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        FuriString* path = furi_string_alloc_set(EXT_PATH("apps_data"));
        DialogsFileBrowserOptions opts;
        dialog_file_browser_set_basic_options(&opts, ".bin", NULL);
        opts.base_path = EXT_PATH("");
        bool ok = dialog_file_browser_show(dialogs, path, path, &opts);
        furi_record_close(RECORD_DIALOGS);
        if(ok) {
            strncpy(app->fw_path, furi_string_get_cstr(path), sizeof(app->fw_path) - 1);
            app->fw_path[sizeof(app->fw_path) - 1] = '\0';
            app->fw_op = 1;
            scene_manager_next_scene(app->scene_manager, ReconSceneFirmwareRun);
        }
        furi_string_free(path);
        return true;
    }
    return false;
}

void recon_scene_firmware_on_exit(void* context) {
    ReconApp* app = context;
    submenu_reset(app->submenu);
}
