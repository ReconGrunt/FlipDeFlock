#include "../recon_app_i.h"
#include "../helpers/esp_link.h"
#include "../helpers/gps_link.h"

// "Net Guardian": a leave-it-on-the-desk monitor. It keeps the ESP worker alive
// and rotates the companion through its detection modes so EVERY WATCHSCORE
// input stays live -- dual-band Flock + deauth (flockcombo), BLE trackers
// (blescan) and evil-twin APs (wifiscan) -- then ticks the fused scorer every
// frame and surfaces it pwnagotchi-style. The Flock/ALPR scan, by contrast,
// only runs flockcombo and never ticks the scorer.

// Rotating sweep: command + how long to dwell before moving on. flockcombo is
// the primary (continuous, two independent radios + deauth) so it gets the
// longest slice; blescan/wifiscan are one-shot sweeps (~6 s) that refresh their
// arrays. Detections persist in the app arrays and the scorer fuses whatever is
// still within its freshness window, so a 38 s cycle stays inside the 60 s
// Flock / 30 s deauth windows.
static const struct {
    const char* cmd;
    uint32_t dwell_ms;
} GUARD_PHASES[] = {
    {"flockcombo", 20000},
    {"blescan", 9000},
    {"wifiscan", 9000},
};
#define GUARD_PHASE_COUNT (sizeof(GUARD_PHASES) / sizeof(GUARD_PHASES[0]))

static bool s_blocked; // companion-only feature opened in Marauder mode
static uint32_t s_phase_mark; // tick of the last phase switch

static void guardian_show_guard(ReconApp* app) {
    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        64,
        "Net Guardian needs the\nFlipDeFlock companion FW\n(it rotates WiFi + BLE).\n\nYou're in Marauder mode\n(Flock detect only).\nFlash via 'ESP32 Firmware'\nor switch Board Mode in\nSettings.");
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

void recon_scene_guardian_on_enter(void* context) {
    ReconApp* app = context;

    // The rotating sweep (blescan/wifiscan) needs the companion protocol; in
    // Marauder mode explain and bail (Flock/ALPR Detect still works there).
    if(app->settings.backend != EspBackendCompanion) {
        s_blocked = true;
        guardian_show_guard(app);
        return;
    }
    s_blocked = false;

    // Fresh baseline so the guardian starts honestly CLEAR rather than off the
    // tail of a previous scan.
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->flock_count = 0;
    app->ble_count = 0;
    app->wifi_count = 0;
    app->deauth_count = 0;
    app->esp_deauths = 0;
    app->esp_frames = 0;
    app->esp_hits = 0;
    app->esp_rebase = true; // per-session rebase off the companion's lifetime total
    app->esp_connected = false;
    furi_mutex_release(app->mutex);

    watchscore_init(&app->watch);
    app->guardian_since = furi_get_tick();
    app->guardian_phase = 0;
    s_phase_mark = app->guardian_since;

    // ESP first so it claims its UART (and disables the expansion manager); GPS
    // only if it's on a different port (else it would steal the ESP's UART).
    app->esp = esp_link_alloc(app);
    esp_link_start(app->esp);
    esp_link_send(app->esp, GUARD_PHASES[0].cmd);

    if(app->settings.gps_enabled && app->settings.gps_uart != app->settings.esp_uart) {
        app->gps = gps_link_alloc(app);
        gps_link_start(app->gps);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewGuardian);
}

bool recon_scene_guardian_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(s_blocked) return false; // Marauder guard screen: let Back exit

    if(event.type == SceneManagerEventTypeTick) {
        // Advance the rotating sweep when the current phase's dwell elapses.
        uint32_t now = furi_get_tick();
        if(now - s_phase_mark >= GUARD_PHASES[app->guardian_phase].dwell_ms) {
            app->guardian_phase = (uint8_t)((app->guardian_phase + 1) % GUARD_PHASE_COUNT);
            if(app->esp) esp_link_send(app->esp, GUARD_PHASES[app->guardian_phase].cmd);
            s_phase_mark = now;
        }

        // Tick the fused scorer live (this also fires the one-shot haptic/sound
        // alert on the transition INTO ELEVATED). On that rising edge also wake
        // the backlight so a guardian across the room is noticeable.
        recon_app_watchscore_tick(app);
        if(app->watch.just_elevated) {
            notification_message(app->notifications, &sequence_display_backlight_on);
        }

        guardian_view_refresh(app->guardian_view);
        return true;
    }
    return false;
}

void recon_scene_guardian_on_exit(void* context) {
    ReconApp* app = context;
    if(app->esp) {
        esp_link_stop(app->esp);
        esp_link_free(app->esp);
        app->esp = NULL;
    }
    if(app->gps) {
        gps_link_stop(app->gps);
        gps_link_free(app->gps);
        app->gps = NULL;
    }
    widget_reset(app->widget);
}
