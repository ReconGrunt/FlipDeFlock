#include "../recon_app_i.h"
#include "../helpers/esp_link.h"
#include "../helpers/wifi_audit.h"
#include "../helpers/recon_report.h"

#include <string.h>

#define WIFI_ITEM_RESCAN 0
#define WIFI_ITEM_SAVE 1
#define WIFI_ITEM_AP_BASE 10
#define WIFI_SCAN_TIMEOUT_MS 9000

// GUI-thread-only scene state (one WiFi-audit scene at a time).
static int s_state; // 0 = scanning, 1 = results, 2 = timeout
static uint32_t s_start;
static bool s_blocked; // companion-only feature opened in Marauder mode

static void recon_scene_wifi_show_guard(ReconApp* app) {
    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        64,
        "WiFi Audit needs the\nFlipDeFlock companion FW.\n\nYou're in Marauder mode\n(Flock detect only).\nFlash via 'ESP32 Firmware'\nor switch Board Mode in\nSettings.");
}

static void recon_scene_wifi_submenu_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void recon_scene_wifi_show_scanning(ReconApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "WiFi Audit");
    submenu_add_item(submenu, "Scanning... please wait", WIFI_ITEM_RESCAN, recon_scene_wifi_submenu_cb, app);
}

static void recon_scene_wifi_show_timeout(ReconApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "WiFi Audit - no data");
    submenu_add_item(submenu, "Rescan", WIFI_ITEM_RESCAN, recon_scene_wifi_submenu_cb, app);
    submenu_add_item(
        submenu, "(needs companion FW)", WIFI_ITEM_RESCAN, recon_scene_wifi_submenu_cb, app);
}

/** Sort the AP list worst-first (by grade, then signal) and build the menu. */
static void recon_scene_wifi_show_results(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    size_t n = app->wifi_count;
    // Insertion sort in place: grade desc, then rssi desc.
    for(size_t i = 1; i < n; i++) {
        WifiAp key = app->wifi[i];
        WifiGrade kg = wifi_audit_grade(key.authmode, key.pairwise, key.wps, key.ssid, NULL);
        int j = (int)i - 1;
        while(j >= 0) {
            WifiAp* a = &app->wifi[j];
            WifiGrade ag = wifi_audit_grade(a->authmode, a->pairwise, a->wps, a->ssid, NULL);
            if(ag > kg || (ag == kg && a->rssi >= key.rssi)) break;
            app->wifi[j + 1] = app->wifi[j];
            j--;
        }
        app->wifi[j + 1] = key;
    }
    // Evil-twin heuristic: the same SSID advertised by more than one distinct
    // BSSID (could be a legit mesh/extender -> "dup"); if those clones run
    // *different* security (e.g. one open, one WPA2) that's a strong rogue/
    // evil-twin signal -> "rogue".
    for(size_t i = 0; i < n; i++) {
        app->wifi[i].dup = false;
        app->wifi[i].rogue = false;
    }
    for(size_t i = 0; i < n; i++) {
        if(app->wifi[i].ssid[0] == '\0') continue;
        for(size_t j = i + 1; j < n; j++) {
            if(strcmp(app->wifi[i].ssid, app->wifi[j].ssid) == 0 &&
               memcmp(app->wifi[i].bssid, app->wifi[j].bssid, 6) != 0) {
                app->wifi[i].dup = true;
                app->wifi[j].dup = true;
                if(app->wifi[i].authmode != app->wifi[j].authmode) {
                    app->wifi[i].rogue = true;
                    app->wifi[j].rogue = true;
                }
            }
        }
    }
    int crit = 0, weak = 0, twin = 0;
    for(size_t i = 0; i < n; i++) {
        WifiAp* a = &app->wifi[i];
        WifiGrade g = wifi_audit_grade(a->authmode, a->pairwise, a->wps, a->ssid, NULL);
        if(g == WifiGradeCritical) crit++;
        else if(g == WifiGradeWeak) weak++;
        if(a->dup || a->rogue) twin++;
    }
    furi_mutex_release(app->mutex);

    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    snprintf(
        app->text_store, RECON_TEXT_STORE, "WiFi: %u  %dC %dW %dT", (unsigned)n, crit, weak, twin);
    submenu_set_header(submenu, app->text_store);
    submenu_add_item(submenu, "Rescan", WIFI_ITEM_RESCAN, recon_scene_wifi_submenu_cb, app);
    submenu_add_item(submenu, "Save Report", WIFI_ITEM_SAVE, recon_scene_wifi_submenu_cb, app);

    for(size_t i = 0; i < n; i++) {
        WifiAp* a = &app->wifi[i];
        WifiGrade g = wifi_audit_grade(a->authmode, a->pairwise, a->wps, a->ssid, NULL);
        char tw[4];
        snprintf(
            tw,
            sizeof(tw),
            "%s%s",
            a->marked ? "*" : "",
            a->rogue ? "!" : (a->dup ? "~" : ""));
        char label[48];
        if(a->ssid[0]) {
            snprintf(label, sizeof(label), "%s%s %s", wifi_grade_str(g), tw, a->ssid);
        } else {
            snprintf(
                label,
                sizeof(label),
                "%s%s [%02X%02X%02X]",
                wifi_grade_str(g),
                tw,
                a->bssid[3],
                a->bssid[4],
                a->bssid[5]);
        }
        submenu_add_item(
            submenu, label, WIFI_ITEM_AP_BASE + i, recon_scene_wifi_submenu_cb, app);
    }
}

static void recon_scene_wifi_trigger(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->wifi_count = 0;
    app->wifi_done = false;
    app->wifi_scanning = false;
    furi_mutex_release(app->mutex);
    if(app->esp) esp_link_send(app->esp, "wifiscan");
    s_state = 0;
    s_start = furi_get_tick();
    recon_scene_wifi_show_scanning(app);
}

void recon_scene_wifi_on_enter(void* context) {
    ReconApp* app = context;

    // The WiFi audit relies on the companion firmware protocol (the only way to
    // get auth/cipher/WPS). In Marauder mode it can't work -> explain instead of
    // showing a dead screen.
    app->saved_backend = app->settings.backend;
    if(app->settings.backend != EspBackendCompanion) {
        s_blocked = true;
        recon_scene_wifi_show_guard(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
        return;
    }
    s_blocked = false;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->esp_connected = false;
    furi_mutex_release(app->mutex);

    app->esp = esp_link_alloc(app);
    esp_link_start(app->esp);

    recon_scene_wifi_trigger(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_wifi_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    bool consumed = false;

    if(s_blocked) return false; // Marauder mode guard screen; let Back exit

    if(event.type == SceneManagerEventTypeTick) {
        if(s_state == 0) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool done = app->wifi_done;
            furi_mutex_release(app->mutex);
            if(done) {
                recon_scene_wifi_show_results(app);
                s_state = 1;
            } else if(furi_get_tick() - s_start > WIFI_SCAN_TIMEOUT_MS) {
                recon_scene_wifi_show_timeout(app);
                s_state = 2;
            }
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        uint32_t id = event.event;
        if(id == WIFI_ITEM_RESCAN) {
            recon_scene_wifi_trigger(app);
            consumed = true;
        } else if(id == WIFI_ITEM_SAVE) {
            char path[128] = {0};
            bool ok = recon_report_save_wifi(app, path, sizeof(path));
            if(app->settings.sound) {
                notification_message(
                    app->notifications, ok ? &sequence_success : &sequence_error);
            }
            consumed = true;
        } else if(id >= WIFI_ITEM_AP_BASE) {
            int idx = (int)id - WIFI_ITEM_AP_BASE;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool valid = idx >= 0 && idx < (int)app->wifi_count;
            furi_mutex_release(app->mutex);
            if(valid) {
                app->wifi_selected = idx;
                scene_manager_next_scene(app->scene_manager, ReconSceneWifiDetail);
            }
            consumed = true;
        }
    }
    return consumed;
}

void recon_scene_wifi_on_exit(void* context) {
    ReconApp* app = context;
    if(app->esp) {
        esp_link_stop(app->esp);
        esp_link_free(app->esp);
        app->esp = NULL;
    }
    app->settings.backend = app->saved_backend; // restore Flock backend choice
    submenu_reset(app->submenu);
}
