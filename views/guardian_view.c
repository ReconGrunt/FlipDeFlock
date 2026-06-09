#include "guardian_view.h"
#include "../recon_app_i.h"
#include "ui_widgets.h"

#include <gui/elements.h>
#include <string.h>

struct GuardianView {
    View* view;
};

typedef struct {
    void* app; /**< ReconApp* */
} GuardianViewModel;

// Pwnagotchi mood per fused state: face + headline + idle tagline.
static const char* guardian_face(WatchState st) {
    switch(st) {
    case WatchStateElevated:
        return "(>_<)";
    case WatchStateWatchful:
        return "(o_o)";
    case WatchStateClear:
    default:
        return "(-_-)";
    }
}

static const char* guardian_word(WatchState st) {
    switch(st) {
    case WatchStateElevated:
        return "ELEVATED";
    case WatchStateWatchful:
        return "WATCHFUL";
    case WatchStateClear:
    default:
        return "CLEAR";
    }
}

static const char* guardian_mode(uint8_t phase) {
    switch(phase) {
    case 1:
        return "BLE";
    case 2:
        return "WiFi";
    case 0:
    default:
        return "WiFi+BLE";
    }
}

// Draw `text` on up to two FontSecondary lines (~21 chars each), splitting on
// the last space that fits so a long breakdown wraps cleanly instead of clipping.
static void draw_wrapped(Canvas* canvas, const char* text, int y) {
    const int kMax = 21;
    int len = (int)strlen(text);
    if(len <= kMax) {
        canvas_draw_str(canvas, 2, y, text);
        return;
    }
    int split = kMax;
    for(int i = kMax; i > 0; i--) {
        if(text[i] == ' ') {
            split = i;
            break;
        }
    }
    char a[24];
    int an = split < (int)sizeof(a) ? split : (int)sizeof(a) - 1;
    memcpy(a, text, an);
    a[an] = '\0';
    canvas_draw_str(canvas, 2, y, a);

    char b[24];
    const char* rest = text + split + (text[split] == ' ' ? 1 : 0);
    snprintf(b, sizeof(b), "%s", rest);
    canvas_draw_str(canvas, 2, y + 9, b);
}

static void guardian_view_draw_callback(Canvas* canvas, void* _model) {
    GuardianViewModel* model = _model;
    ReconApp* app = model->app;
    if(!app) return;

    // The draw runs on the GUI thread while the scene tick writes these on the
    // app thread, so snapshot everything shared under the lock (same discipline
    // as flock_view). `breakdown` is copied out so a mid-update string can't be
    // read torn.
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool connected = app->esp_connected;
    uint32_t hits = app->esp_hits;
    uint8_t channel = app->esp_channel;
    WatchState st = (WatchState)app->watch.state;
    int score = (int)app->watch.score; // threat meter pct (0..100)
    char bd[WATCHSCORE_BREAKDOWN_LEN];
    snprintf(bd, sizeof(bd), "%s", app->watch.breakdown);
    furi_mutex_release(app->mutex);

    uint8_t phase = app->guardian_phase;
    bool wifi_only = (app->settings.backend != EspBackendCompanion);

    uint32_t secs = (furi_get_tick() - app->guardian_since) / 1000;
    char up[16];
    snprintf(
        up,
        sizeof(up),
        "%lu:%02lu:%02lu",
        (unsigned long)(secs / 3600),
        (unsigned long)((secs / 60) % 60),
        (unsigned long)(secs % 60));

    canvas_clear(canvas);

    // --- HUD title bar (inverted): "NET GUARDIAN" + uptime ------------------
    // Leaves color=black, font=Secondary.
    ui_title_bar(canvas, "NET GUARDIAN", up);

    if(!connected) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 30, "connecting ESP32...");
        canvas_draw_str(canvas, 2, 42, "hold BOOT, tap RESET");
        return;
    }

    // --- mascot + state word (the centerpiece) ------------------------------
    // Face on its own bigger row; state word in FontPrimary beside it. When
    // ELEVATED, the word gets a filled box (loud) but the mascot stays plain so
    // it always reads.
    canvas_set_font(canvas, FontPrimary);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str(canvas, 2, 29, guardian_face(st));

    if(st == WatchStateElevated) {
        canvas_draw_box(canvas, 40, 18, 88, 13);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 44, 28, guardian_word(st));
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, 44, 28, guardian_word(st));
    }

    // --- THREAT meter -------------------------------------------------------
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 42, "thr");
    ui_meter(canvas, 36, 35, 90, 8, score);

    // --- footer: scan mode + channel + hits ---------------------------------
    char foot[28];
    snprintf(
        foot,
        sizeof(foot),
        "%s  ch%u  hits %lu",
        guardian_mode(phase),
        channel,
        (unsigned long)hits);
    canvas_draw_str(canvas, 2, 52, foot);

    // --- breakdown / tagline ------------------------------------------------
    if(bd[0]) {
        draw_wrapped(canvas, bd, 62);
    } else if(st == WatchStateClear) {
        canvas_draw_str(
            canvas, 2, 62, wifi_only ? "all quiet (WiFi only)" : "all quiet. watching.");
    }
}

// Guardian has no list to navigate: ignore all keys so Back bubbles up to the
// scene manager (which exits the scene and tears the radio down).
static bool guardian_view_input_callback(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return false;
}

GuardianView* guardian_view_alloc(void) {
    GuardianView* gv = malloc(sizeof(GuardianView));
    gv->view = view_alloc();
    view_set_context(gv->view, gv);
    view_allocate_model(gv->view, ViewModelTypeLocking, sizeof(GuardianViewModel));
    view_set_draw_callback(gv->view, guardian_view_draw_callback);
    view_set_input_callback(gv->view, guardian_view_input_callback);
    with_view_model(gv->view, GuardianViewModel * model, { model->app = NULL; }, false);
    return gv;
}

void guardian_view_free(GuardianView* gv) {
    view_free(gv->view);
    free(gv);
}

View* guardian_view_get_view(GuardianView* gv) {
    return gv->view;
}

void guardian_view_set_app(GuardianView* gv, void* app) {
    with_view_model(gv->view, GuardianViewModel * model, { model->app = app; }, false);
}

void guardian_view_refresh(GuardianView* gv) {
    with_view_model(gv->view, GuardianViewModel * model, { UNUSED(model); }, true);
}
