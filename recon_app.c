#include "recon_app_i.h"
#include "helpers/esp_link.h"
#include "helpers/gps_link.h"
#include "helpers/recon_report.h"

#include <math.h>
#include <string.h>

#define RECON_TICK_MS 250

// Anti-stalking "following" gate. A real tracker following you clears all four
// of these easily; stationary shop Tiles and a single drive-by past a fixed
// beacon do not. All tunable; this only TIGHTENS precision (never flags more
// loosely than the old single >100 m gate).
#define FOLLOW_MIN_COUNT 4 /**< seen at least this many scans */
#define FOLLOW_MIN_MS 90000 /**< over at least this long a window (90 s) */
#define FOLLOW_MIN_WAYPOINTS 3 /**< at this many distinct observer waypoints */
#define WAYPOINT_GAP_M 50.0f /**< min separation to count a new waypoint */
#define FOLLOW_MIN_SPAN_M 150.0f /**< max span between counted waypoints */

// ---- shared data updates (called from worker threads) --------------------

void recon_app_report_flock(
    ReconApp* app,
    const uint8_t mac[6],
    const char* ssid,
    int8_t rssi,
    uint8_t channel,
    char ftype,
    FlockConfidence confidence) {
    if(confidence == FlockConfidenceNone) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    uint32_t now = furi_get_tick();
    FlockEntry* entry = NULL;
    for(size_t i = 0; i < app->flock_count; i++) {
        if(memcmp(app->flock[i].mac, mac, 6) == 0) {
            entry = &app->flock[i];
            break;
        }
    }

    if(!entry && app->flock_count < RECON_FLOCK_MAX) {
        entry = &app->flock[app->flock_count++];
        memset(entry, 0, sizeof(FlockEntry));
        memcpy(entry->mac, mac, 6);
        entry->first_tick = now;
        entry->lat = NAN;
        entry->lon = NAN;
        entry->heading = NAN;
        entry->count = 0;
    }

    if(entry) {
        entry->count++;
        entry->last_tick = now;
        if(rssi != 0) entry->rssi = rssi;
        if(channel != 0) entry->channel = channel;
        if(ftype) entry->ftype = ftype;
        if(confidence > entry->confidence) entry->confidence = confidence;
        if(ssid && ssid[0] && entry->ssid[0] == '\0') {
            strncpy(entry->ssid, ssid, RECON_SSID_LEN - 1);
            entry->ssid[RECON_SSID_LEN - 1] = '\0';
        }
        // Geotag with the current fix if we have one and haven't tagged yet, or
        // refresh only to a meaningfully stronger sighting. RSSI oscillates
        // +/-5-10 dB scan-to-scan, so a 6 dB hysteresis stops the tag jittering.
        if(app->gps_valid && (isnan(entry->lat) || rssi > entry->geotag_rssi + 6)) {
            entry->lat = app->gps_lat;
            entry->lon = app->gps_lon;
            entry->heading = app->gps_course;
            entry->geotag_rssi = rssi;
        }
    }

    furi_mutex_release(app->mutex);
}

void recon_app_set_esp_status(
    ReconApp* app,
    uint32_t frames,
    uint32_t hits,
    uint8_t channel,
    bool connected) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->esp_connected = connected;
    // (0,0,0) is a keepalive/banner; don't clobber real counters with it.
    if(!(frames == 0 && hits == 0 && channel == 0)) {
        app->esp_frames = frames;
        app->esp_hits = hits;
        app->esp_channel = channel;
    }
    furi_mutex_release(app->mutex);
}

void recon_app_set_esp_lines(ReconApp* app, uint32_t lines) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->esp_lines = lines;
    app->esp_connected = true;
    furi_mutex_release(app->mutex);
}

void recon_app_set_deauths(ReconApp* app, uint32_t deauths) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->esp_deauths = deauths;
    furi_mutex_release(app->mutex);
}

/** Rough planar distance in metres (good enough for a >100 m "moved" test). */
static float recon_dist_m(float lat1, float lon1, float lat2, float lon2) {
    float dlat = (lat2 - lat1) * 111320.0f;
    float dlon = (lon2 - lon1) * 111320.0f * cosf(lat1 * (float)M_PI / 180.0f);
    return sqrtf(dlat * dlat + dlon * dlon);
}

void recon_app_ble_begin(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->ble_scanning = true;
    app->ble_done = false;
    app->esp_connected = true;
    furi_mutex_release(app->mutex);
}

void recon_app_ble_add(
    ReconApp* app,
    const uint8_t addr[6],
    const char* name,
    int8_t rssi,
    uint8_t cat,
    uint16_t company) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    uint32_t now = furi_get_tick();
    BleDevice* e = NULL;
    for(size_t i = 0; i < app->ble_count; i++) {
        if(memcmp(app->ble[i].addr, addr, 6) == 0) {
            e = &app->ble[i];
            break;
        }
    }
    if(!e && app->ble_count < RECON_BLE_MAX) {
        e = &app->ble[app->ble_count++];
        memset(e, 0, sizeof(BleDevice));
        memcpy(e->addr, addr, 6);
        e->first_lat = app->gps_valid ? app->gps_lat : NAN;
        e->first_lon = app->gps_valid ? app->gps_lon : NAN;
        e->first_tick = now;
        e->last_tick = now;
        // First counted waypoint is wherever we are now (NAN until we get a fix).
        e->last_wp_lat = app->gps_valid ? app->gps_lat : NAN;
        e->last_wp_lon = app->gps_valid ? app->gps_lon : NAN;
        e->inrange_wp_count = app->gps_valid ? 1 : 0;
        e->max_span_m = 0.0f;
    }
    if(e) {
        e->count++;
        e->rssi = rssi;
        if(cat) e->cat = cat;
        e->company = company;
        if(name && name[0] && e->name[0] == '\0') {
            strncpy(e->name, name, RECON_SSID_LEN - 1);
            e->name[RECON_SSID_LEN - 1] = '\0';
        }
        if(app->gps_valid) {
            e->last_lat = app->gps_lat;
            e->last_lon = app->gps_lon;
            e->last_tick = now;
            // First fix may arrive after creation (created with no GPS): seed the
            // first counted waypoint here so the span/waypoint logic can start.
            if(isnan(e->last_wp_lat)) {
                e->last_wp_lat = app->gps_lat;
                e->last_wp_lon = app->gps_lon;
                if(e->inrange_wp_count == 0) e->inrange_wp_count = 1;
            } else if(
                recon_dist_m(e->last_wp_lat, e->last_wp_lon, app->gps_lat, app->gps_lon) >=
                WAYPOINT_GAP_M) {
                // We've moved a fresh waypoint's distance and the device is still
                // in range: count it, advance the marker, grow the track span.
                e->inrange_wp_count++;
                e->last_wp_lat = app->gps_lat;
                e->last_wp_lon = app->gps_lon;
                if(!isnan(e->first_lat)) {
                    float span =
                        recon_dist_m(e->first_lat, e->first_lon, app->gps_lat, app->gps_lon);
                    if(span > e->max_span_m) e->max_span_m = span;
                }
            }
            // "Following": a tracker seen across many scans, over a real time
            // window, at several distinct observer waypoints, spanning real
            // ground is the anti-stalking signal. AND of all four (latched).
            if(e->count >= FOLLOW_MIN_COUNT && (now - e->first_tick) >= FOLLOW_MIN_MS &&
               e->inrange_wp_count >= FOLLOW_MIN_WAYPOINTS && e->max_span_m >= FOLLOW_MIN_SPAN_M) {
                e->following = true;
            }
        }
    }
    furi_mutex_release(app->mutex);

    // A BLE-classified Flock/Raven device is also a Flock detection -> merge it
    // into the Flock list (ftype 'L' = BLE) so it shows alongside WiFi hits,
    // gets geotagged, and lands in reports. (Done after releasing the mutex;
    // recon_app_report_flock takes it itself.)
    if(cat == BleCatFlock) {
        recon_app_report_flock(app, addr, name, rssi, 0, 'L', FlockConfidenceConfirmed);
    }
}

void recon_app_ble_end(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->ble_scanning = false;
    app->ble_done = true;
    furi_mutex_release(app->mutex);
}

void recon_app_add_deauth_target(ReconApp* app, const uint8_t bssid[6], uint8_t channel) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t now = furi_get_tick();
    DeauthTarget* t = NULL;
    for(size_t i = 0; i < app->deauth_count; i++) {
        if(memcmp(app->deauth[i].bssid, bssid, 6) == 0) {
            t = &app->deauth[i];
            break;
        }
    }
    if(!t && app->deauth_count < RECON_DEAUTH_MAX) {
        t = &app->deauth[app->deauth_count++];
        memset(t, 0, sizeof(DeauthTarget));
        memcpy(t->bssid, bssid, 6);
    }
    if(t) {
        t->count++;
        if(channel) t->channel = channel;
        t->last_tick = now;
    }
    furi_mutex_release(app->mutex);
}

void recon_app_wifi_begin(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->wifi_count = 0;
    app->wifi_scanning = true;
    app->wifi_done = false;
    app->esp_connected = true;
    furi_mutex_release(app->mutex);
}

void recon_app_wifi_add(
    ReconApp* app,
    const uint8_t bssid[6],
    const char* ssid,
    int8_t rssi,
    uint8_t channel,
    uint8_t authmode,
    uint8_t pairwise,
    bool wps) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->wifi_count < RECON_WIFI_MAX) {
        WifiAp* ap = &app->wifi[app->wifi_count++];
        memset(ap, 0, sizeof(WifiAp));
        memcpy(ap->bssid, bssid, 6);
        if(ssid) {
            strncpy(ap->ssid, ssid, RECON_SSID_LEN - 1);
            ap->ssid[RECON_SSID_LEN - 1] = '\0';
        }
        ap->rssi = rssi;
        ap->channel = channel;
        ap->authmode = authmode;
        ap->pairwise = pairwise;
        ap->wps = wps;
    }
    furi_mutex_release(app->mutex);
}

void recon_app_wifi_end(ReconApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->wifi_scanning = false;
    app->wifi_done = true;
    furi_mutex_release(app->mutex);
}

// ---- settings ------------------------------------------------------------

static void recon_settings_defaults(ReconApp* app) {
    app->settings.backend = EspBackendCompanion;
    app->settings.esp_uart = FuriHalSerialIdUsart;
    app->settings.gps_uart = FuriHalSerialIdLpuart;
    app->settings.esp_baud = 115200;
    app->settings.gps_baud = 9600;
    app->settings.marauder_cmd = 0; // sniffprobe
    app->settings.gps_enabled = false; // off by default
    app->settings.sound = true;
    app->settings.flash_fast = false; // safe 115200 by default
}

void recon_settings_save(ReconApp* app) {
    recon_report_ensure_dirs(app);
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, RECON_SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* s = furi_string_alloc();
        furi_string_printf(
            s,
            "backend=%d\nesp_uart=%d\ngps_uart=%d\nesp_baud=%lu\ngps_baud=%lu\nmarauder_cmd=%d\ngps_enabled=%d\nsound=%d\nflash_fast=%d\n",
            app->settings.backend,
            app->settings.esp_uart,
            app->settings.gps_uart,
            (unsigned long)app->settings.esp_baud,
            (unsigned long)app->settings.gps_baud,
            app->settings.marauder_cmd,
            app->settings.gps_enabled ? 1 : 0,
            app->settings.sound ? 1 : 0,
            app->settings.flash_fast ? 1 : 0);
        storage_file_write(file, furi_string_get_cstr(s), furi_string_size(s));
        furi_string_free(s);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void recon_settings_apply_kv(ReconApp* app, const char* key, long val) {
    if(strcmp(key, "backend") == 0)
        app->settings.backend = (val == EspBackendGeneric) ? EspBackendGeneric : EspBackendCompanion;
    else if(strcmp(key, "esp_uart") == 0)
        app->settings.esp_uart = (val == FuriHalSerialIdLpuart) ? FuriHalSerialIdLpuart : FuriHalSerialIdUsart;
    else if(strcmp(key, "gps_uart") == 0)
        app->settings.gps_uart = (val == FuriHalSerialIdUsart) ? FuriHalSerialIdUsart : FuriHalSerialIdLpuart;
    else if(strcmp(key, "esp_baud") == 0 && (val == 115200 || val == 921600))
        app->settings.esp_baud = (uint32_t)val; // clamp to known-valid; corrupt -> keep default
    else if(strcmp(key, "gps_baud") == 0 && (val == 9600 || val == 57600 || val == 115200))
        app->settings.gps_baud = (uint32_t)val;
    else if(strcmp(key, "marauder_cmd") == 0 && val >= 0 && val < 4)
        app->settings.marauder_cmd = (uint8_t)val;
    else if(strcmp(key, "gps_enabled") == 0)
        app->settings.gps_enabled = (val != 0);
    else if(strcmp(key, "sound") == 0)
        app->settings.sound = (val != 0);
    else if(strcmp(key, "flash_fast") == 0)
        app->settings.flash_fast = (val != 0);
}

void recon_settings_load(ReconApp* app) {
    recon_settings_defaults(app);
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, RECON_SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[256];
        size_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[n] = '\0';
        char* line = buf;
        while(line && *line) {
            char* nl = strchr(line, '\n');
            if(nl) *nl = '\0';
            char* eq = strchr(line, '=');
            if(eq) {
                *eq = '\0';
                recon_settings_apply_kv(app, line, strtol(eq + 1, NULL, 10));
            }
            line = nl ? nl + 1 : NULL;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

// ---- view dispatcher glue ------------------------------------------------

static bool recon_custom_event_callback(void* context, uint32_t event) {
    ReconApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool recon_back_event_callback(void* context) {
    ReconApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void recon_tick_event_callback(void* context) {
    ReconApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

// ---- lifecycle -----------------------------------------------------------

static ReconApp* recon_app_alloc(void) {
    ReconApp* app = malloc(sizeof(ReconApp));
    memset(app, 0, sizeof(ReconApp));

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->fw_log = furi_string_alloc();
    app->gps_lat = NAN;
    app->gps_lon = NAN;
    app->gps_course = NAN;

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    recon_settings_load(app);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&recon_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, recon_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, recon_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, recon_tick_event_callback, RECON_TICK_MS);

    app->submenu = submenu_alloc();
    app->var_item_list = variable_item_list_alloc();
    app->widget = widget_alloc();
    app->popup = popup_alloc();
    app->flock_view = flock_view_alloc();
    flock_view_set_app(app->flock_view, app);
    app->flock_map_view = flock_map_view_alloc();
    flock_map_view_set_app(app->flock_map_view, app);
    app->deflock_qr_view = deflock_qr_view_alloc();
    deflock_qr_view_set_app(app->deflock_qr_view, app);

    view_dispatcher_add_view(
        app->view_dispatcher, ReconViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, ReconViewVarItemList, variable_item_list_get_view(app->var_item_list));
    view_dispatcher_add_view(app->view_dispatcher, ReconViewWidget, widget_get_view(app->widget));
    view_dispatcher_add_view(app->view_dispatcher, ReconViewPopup, popup_get_view(app->popup));
    view_dispatcher_add_view(
        app->view_dispatcher, ReconViewFlock, flock_view_get_view(app->flock_view));
    view_dispatcher_add_view(
        app->view_dispatcher, ReconViewFlockMap, flock_map_view_get_view(app->flock_map_view));
    view_dispatcher_add_view(
        app->view_dispatcher, ReconViewDeflockQr, deflock_qr_view_get_view(app->deflock_qr_view));

    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void recon_app_free(ReconApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewFlock);
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewFlockMap);
    view_dispatcher_remove_view(app->view_dispatcher, ReconViewDeflockQr);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    popup_free(app->popup);
    flock_view_free(app->flock_view);
    flock_map_view_free(app->flock_map_view);
    deflock_qr_view_free(app->deflock_qr_view);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);

    furi_string_free(app->fw_log);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t recon_site_survey_app(void* arg) {
    UNUSED(arg);
    ReconApp* app = recon_app_alloc();

    scene_manager_next_scene(app->scene_manager, ReconSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    recon_app_free(app);
    return 0;
}
