#include "../recon_app_i.h"
#include "../helpers/esp_link.h"
#include "../helpers/recon_report.h"

#include <string.h>

#define BLE_ITEM_SAVE 0
#define BLE_ITEM_BASE 10
#define BLE_RESCAN_GAP_MS 4000
#define BLE_SCAN_TIMEOUT_MS 12000

static bool s_pending; // a blescan is in flight (awaiting BEND)
static uint32_t s_mark; // tick of last state transition

static const char* ble_cat_str(uint8_t cat) {
    switch(cat) {
    case BleCatFlock:
        return "FLOCK";
    case BleCatAirTag:
        return "AirTag";
    case BleCatTile:
        return "Tile";
    case BleCatSmartTag:
        return "Tag";
    default:
        return "BLE";
    }
}

static int ble_rank(const BleDevice* d) {
    // following > tracker-category > plain BLE
    return (d->following ? 2 : 0) + (d->cat != BleCatUnknown ? 1 : 0);
}

static void ble_submenu_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void ble_trigger(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->ble_done = false;
    furi_mutex_release(app->mutex);
    if(app->esp) esp_link_send(app->esp, "blescan");
    s_pending = true;
    s_mark = furi_get_tick();
}

static void ble_show_scanning(ReconApp* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "BLE / Tracker scan");
    submenu_add_item(app->submenu, "Scanning (~6s)...", BLE_ITEM_SAVE, ble_submenu_cb, app);
}

static void ble_show_results(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    size_t n = app->ble_count;
    for(size_t i = 1; i < n; i++) {
        BleDevice key = app->ble[i];
        int kr = ble_rank(&key);
        int j = (int)i - 1;
        while(j >= 0) {
            BleDevice* a = &app->ble[j];
            int ar = ble_rank(a);
            if(ar > kr || (ar == kr && a->rssi >= key.rssi)) break;
            app->ble[j + 1] = app->ble[j];
            j--;
        }
        app->ble[j + 1] = key;
    }
    int track = 0, follow = 0;
    for(size_t i = 0; i < n; i++) {
        if(app->ble[i].cat != BleCatUnknown) track++;
        if(app->ble[i].following) follow++;
    }
    furi_mutex_release(app->mutex);

    submenu_reset(app->submenu);
    snprintf(
        app->text_store, RECON_TEXT_STORE, "BLE:%u trk:%d flw:%d", (unsigned)n, track, follow);
    submenu_set_header(app->submenu, app->text_store);
    submenu_add_item(app->submenu, "Save Report", BLE_ITEM_SAVE, ble_submenu_cb, app);

    for(size_t i = 0; i < n; i++) {
        BleDevice* d = &app->ble[i];
        char pfx[4];
        snprintf(pfx, sizeof(pfx), "%s%s", d->marked ? "*" : "", d->following ? "!" : "");
        char label[48];
        if(d->name[0]) {
            snprintf(
                label,
                sizeof(label),
                "%s%s %s %ddB",
                pfx,
                ble_cat_str(d->cat),
                d->name,
                d->rssi);
        } else {
            snprintf(
                label,
                sizeof(label),
                "%s%s %02X%02X%02X %ddB",
                pfx,
                ble_cat_str(d->cat),
                d->addr[3],
                d->addr[4],
                d->addr[5],
                d->rssi);
        }
        submenu_add_item(app->submenu, label, BLE_ITEM_BASE + i, ble_submenu_cb, app);
    }
}

void recon_scene_ble_on_enter(void* context) {
    ReconApp* app = context;
    // BLE scan needs the companion firmware protocol.
    app->saved_backend = app->settings.backend;
    app->settings.backend = EspBackendCompanion;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->ble_count = 0;
    app->ble_done = false;
    app->esp_connected = false;
    furi_mutex_release(app->mutex);

    app->esp = esp_link_alloc(app);
    esp_link_start(app->esp);

    ble_show_scanning(app);
    ble_trigger(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_ble_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool done = app->ble_done;
        furi_mutex_release(app->mutex);
        uint32_t now = furi_get_tick();
        if(s_pending && done) {
            ble_show_results(app);
            s_pending = false;
            s_mark = now;
        } else if(s_pending && now - s_mark > BLE_SCAN_TIMEOUT_MS) {
            s_pending = false;
            s_mark = now; // give up; allow a rescan
        } else if(!s_pending && now - s_mark > BLE_RESCAN_GAP_MS) {
            ble_trigger(app); // continuous monitoring -> "following" detection
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        uint32_t id = event.event;
        if(id == BLE_ITEM_SAVE) {
            char path[128] = {0};
            bool ok = recon_report_save_ble(app, path, sizeof(path));
            if(app->settings.sound) {
                notification_message(
                    app->notifications, ok ? &sequence_success : &sequence_error);
            }
            consumed = true;
        } else if(id >= BLE_ITEM_BASE) {
            int idx = (int)id - BLE_ITEM_BASE;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool valid = idx >= 0 && idx < (int)app->ble_count;
            furi_mutex_release(app->mutex);
            if(valid) {
                app->ble_selected = idx;
                scene_manager_next_scene(app->scene_manager, ReconSceneBleDetail);
            }
            consumed = true;
        }
    }
    return consumed;
}

void recon_scene_ble_on_exit(void* context) {
    ReconApp* app = context;
    if(app->esp) {
        esp_link_stop(app->esp);
        esp_link_free(app->esp);
        app->esp = NULL;
    }
    app->settings.backend = app->saved_backend;
    submenu_reset(app->submenu);
}
