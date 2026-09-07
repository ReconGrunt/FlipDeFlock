// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "../recon_app_i.h"
#include "../helpers/plugin_host.h"
#include "../plugins/flasher_plugin_api.h"

#include <string.h>

// The flasher is an on-demand .fal, not app code. Mapped in on entry to this
// screen and dropped on the way out -- ~27.6 KB that every other screen, and the
// launch itself, no longer has to find room for.
//
// File-scope because the WORKER THREAD needs the API and only ever gets `app` as
// its context, and because only one flash or backup can be in flight at a time
// (this scene owns the thread). Same shape as g_qr_plugin in the handoff scene.
static PluginHost* g_fw_plugin = NULL;
static const FlasherPluginApi* g_fw_api = NULL;

static void fw_log_cb(void* ctx, const char* line) {
    ReconApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    furi_string_cat_printf(app->fw_log, "%s\n", line);
    app->fw_log_dirty = true;
    furi_mutex_release(app->mutex);
}

static void fw_progress_cb(void* ctx, int pct) {
    ReconApp* app = ctx;
    if(pct < 0) pct = 0;
    if(pct > 100) pct = 100;
    app->fw_pct = pct; // volatile int, single writer -- no lock needed
    app->fw_log_dirty = true;
}

static int32_t fw_worker(void* context) {
    ReconApp* app = context;
    const FlasherPluginApi* api = g_fw_api;
    // on_enter refuses to start this thread without a loaded plugin, so this is
    // belt-and-braces rather than an expected path -- but it is called from a
    // thread, and a null deref here would take the whole app down mid-flash.
    if(!api) {
        fw_log_cb(app, "Flasher unavailable.");
        fw_log_cb(app, "== FAILED ==");
        app->fw_ok = false;
        app->fw_running = false;
        return 0;
    }
    EspFlasher* fl = api->alloc((FuriHalSerialId)app->settings.esp_uart, fw_log_cb, fw_progress_cb, app);
    bool ok = false;
    if(!fl) {
        fw_log_cb(app, "UART busy.");
    } else {
        // Both flash (write) and backup (read) talk to the raw ROM loader -- no
        // stub is ever uploaded (the 0xchocolate approach), so the "loader
        // resident / overlapping address" error can't occur. Backup forces Safe
        // baud (ROM reads are slow + integrity matters); flash allows the user's
        // Fast (230400) and verifies the write afterwards.
        uint32_t fast = (app->fw_op == 0 || !app->settings.flash_fast) ? 0 : 230400;
        if(api->connect(fl, fast)) {
            if(app->fw_op == 0) {
                ok = api->backup(fl, app->storage, app->fw_path);
            } else {
                ok = api->flash_file(fl, app->storage, app->fw_path, 0);
            }
        }
        api->free(fl);
    }
    fw_log_cb(app, ok ? "== DONE ==" : "== FAILED ==");
    app->fw_ok = ok;
    app->fw_running = false;
    return 0;
}

// Render the status tail, plus a SINGLE progress bar that stays in one place.
//
// The percentage used to be log lines, so a flash pushed eleven of them into a
// scrolling text box and the operator had to scroll to see where it was. A
// transfer in progress now gets a fixed three-line layout with the bar pinned to
// the bottom; the scrolling log is kept only for the idle/finished case, where
// what matters is reading an error message rather than watching a number.
static void fw_render(ReconApp* app) {
    widget_reset(app->widget);
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    const char* full = furi_string_get_cstr(app->fw_log);
    size_t len = strlen(full);
    int pct = app->fw_pct;

    if(pct >= 0) {
        // Last non-empty log line = what the flasher is currently doing.
        const char* last = full;
        for(int i = (int)len - 2; i >= 0; i--) {
            if(full[i] == '\n') {
                last = full + i + 1;
                break;
            }
        }
        snprintf(app->fw_status, sizeof(app->fw_status), "%s", last);
        char* nl = strchr(app->fw_status, '\n');
        if(nl) *nl = '\0';

        // 12 cells at ~6 px each keeps "[############] 100%" inside 128 px with
        // the default font. Integer maths only -- no floats on this path.
        char bar[40];
        int filled = (pct * 12) / 100;
        int o = 0;
        bar[o++] = '[';
        for(int i = 0; i < 12; i++) bar[o++] = (i < filled) ? '#' : '-';
        bar[o++] = ']';
        snprintf(bar + o, sizeof(bar) - (size_t)o, " %d%%", pct);

        widget_add_string_element(
            app->widget, 0, 2, AlignLeft, AlignTop, FontPrimary,
            app->fw_op == 0 ? "Backing up..." : "Flashing...");
        widget_add_string_element(
            app->widget, 0, 22, AlignLeft, AlignTop, FontSecondary, app->fw_status);
        widget_add_string_element(
            app->widget, 64, 44, AlignCenter, AlignTop, FontPrimary, bar);
        furi_mutex_release(app->mutex);
        return;
    }

    // Not transferring: show the tail of the log, scrollable, so a failure
    // message is fully readable.
    const char* start = full;
    int nl = 0;
    for(int i = (int)len - 1; i >= 0; i--) {
        if(full[i] == '\n') {
            if(++nl >= 8) {
                start = full + i + 1;
                break;
            }
        }
    }
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, start);
    furi_mutex_release(app->mutex);
}

void recon_scene_firmware_run_on_enter(void* context) {
    ReconApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    furi_string_reset(app->fw_log);
    app->fw_pct = -1; // no transfer yet -- the log view, not the bar
    furi_mutex_release(app->mutex);

    fw_log_cb(app, app->fw_op == 0 ? "BACKUP firmware" : "FLASH firmware");
    fw_log_cb(app, "Put ESP in bootloader:");
    fw_log_cb(app, "hold BOOT, tap RESET.");
    fw_log_cb(app, "Working...");

    // The worker needs a chunk of heap (4 KB thread stack + UART buffer + the
    // esp-serial-flasher stub upload). A FAP shares the Flipper's ~256 KB RAM
    // with the firmware, so on a busy system this can come up short. Check up
    // front and fail with a message instead of letting an allocation abort the
    // whole app (the "out of memory" crash).
    if(memmgr_get_free_heap() < 10 * 1024) {
        fw_log_cb(app, "Not enough free RAM.");
        fw_log_cb(app, "Reboot the Flipper, open");
        fw_log_cb(app, "only FlipDeFlock, retry.");
        fw_log_cb(app, "== FAILED ==");
        app->fw_running = false;
        app->fw_ok = false;
        fw_render(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
        return;
    }

    // Map the flasher in for the lifetime of this screen. Deliberately AFTER the
    // heap check: the .fal needs room too, so asking for it on a device that is
    // already short would just turn a readable message into a failed load.
    //
    // A failure here is not a crash -- plugin_host_load() returns NULL for a
    // missing asset directory, a version mismatch or a corrupt .fal, and the
    // user gets told the feature is unavailable and can back out. The likeliest
    // real cause is a card the firmware has not extracted app assets onto yet.
    g_fw_api = NULL;
    g_fw_plugin = plugin_host_load(
        FLASHER_PLUGIN_APP_ID, FLASHER_PLUGIN_API_VERSION, (const void**)&g_fw_api);
    if(!g_fw_plugin || !g_fw_api) {
        plugin_host_free(g_fw_plugin);
        g_fw_plugin = NULL;
        g_fw_api = NULL;
        // Name the actual cause instead of guessing at one. The old text said
        // "reinstall the .fap so its assets are extracted", which is only ONE of
        // the reasons this fails and was simply wrong for issue #23 -- a
        // RogueMaster user whose assets were extracted under a different
        // directory name than the running app resolved to. Being told to
        // reinstall something already installed correctly wastes the reporter's
        // time and tells the maintainer nothing.
        //
        // The log carries every path that was tried (see plugin_host.c); this
        // points at it, because a screenshot of this screen is what actually
        // arrives on an issue.
        fw_log_cb(app, "Flasher plugin not found.");
        fw_log_cb(app, "Checked /ext/apps_assets/");
        fw_log_cb(app, "<app name>/plugins/.");
        fw_log_cb(app, "Try: reinstall the .fap,");
        fw_log_cb(app, "or send the CLI log -- it");
        fw_log_cb(app, "lists every path tried.");
        fw_log_cb(app, "== FAILED ==");
        app->fw_running = false;
        app->fw_ok = false;
        fw_render(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
        return;
    }

    app->fw_running = true;
    app->fw_ok = false;
    app->fw_log_dirty = false;
    app->fw_thread = furi_thread_alloc_ex("FlipDeFlockFlash", 4096, fw_worker, app);
    furi_thread_start(app->fw_thread);

    fw_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

bool recon_scene_firmware_run_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        // Only rebuild the widget when the log actually changed.
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool dirty = app->fw_log_dirty;
        app->fw_log_dirty = false;
        furi_mutex_release(app->mutex);
        if(dirty) fw_render(app);
        return true;
    }
    return false;
}

void recon_scene_firmware_run_on_exit(void* context) {
    ReconApp* app = context;
    // ORDER IS LOAD-BEARING. The worker is executing code that lives inside the
    // mapped .fal, so the plugin must outlive the thread: abort, JOIN, and only
    // then unmap. Freeing first would pull the text out from under a running
    // thread mid-flash, which is both a crash and a half-written ESP32.
    if(app->fw_thread) {
        if(g_fw_api) g_fw_api->abort(); // stop a long flash/backup so join returns
        furi_thread_join(app->fw_thread);
        furi_thread_free(app->fw_thread);
        app->fw_thread = NULL;
    }
    g_fw_api = NULL; // drop the borrowed pointer before unmapping what it points into
    plugin_host_free(g_fw_plugin);
    g_fw_plugin = NULL;
    widget_reset(app->widget);
}
