#include "flock_view.h"
#include "../recon_app_i.h"
#include "ui_widgets.h"

#include <gui/elements.h>

#define ROW_H            11
#define LIST_TOP         27
#define VISIBLE_ROWS     3
// Deauth/disassoc frames per ~1s interval needed to call it a flood. Normal
// roaming/idle churn is 1-2/s; a real flood is many. Below this we don't alert
// (avoids false positives on benign disassoc churn).
#define DEAUTH_FLOOD_MIN 5

struct FlockView {
    View* view;
    FlockViewOkCallback ok_cb;
    void* ok_ctx;
};

typedef struct {
    void* app; /**< ReconApp* */
    int selected;
    int top;
} FlockViewModel;

static char confidence_char(FlockConfidence c) {
    switch(c) {
    case FlockConfidenceConfirmed:
        return '!';
    case FlockConfidenceProbeFp:
        return 'F'; // B1 IE-fingerprint class match
    case FlockConfidenceLikely:
        return 'L';
    case FlockConfidencePossible:
        return 'p';
    default:
        return '.';
    }
}

static void flock_view_draw_callback(Canvas* canvas, void* _model) {
    FlockViewModel* model = _model;
    ReconApp* app = model->app;
    if(!app) return;

    canvas_clear(canvas);

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    size_t count = app->flock_count;
    bool connected = app->esp_connected;
    uint32_t frames = app->esp_frames;
    uint32_t hits = app->esp_hits;
    uint8_t channel = app->esp_channel;
    uint32_t lines = app->esp_lines;
    uint32_t deauths = app->esp_deauths;
    bool generic = (app->settings.backend == EspBackendGeneric);
    bool gps_valid = app->gps_valid;
    int gps_sats = app->gps_sats;

    // Header / status bar. A non-zero deauth count is an attack indicator and
    // takes over the header (drops channel/count to make room for the alert).
    // Compact right-aligned status for the inverted title bar.
    char right[12];
    if(deauths >= DEAUTH_FLOOD_MIN) {
        snprintf(right, sizeof(right), "!DEAUTH");
    } else if(generic) {
        snprintf(right, sizeof(right), "rx %lu", (unsigned long)lines);
    } else {
        snprintf(right, sizeof(right), "ch%u h%lu", channel, (unsigned long)hits);
    }
    ui_title_bar(canvas, "FLOCK / ALPR", right); // leaves color=black, font=Secondary

    // Fuller status sub-line under the bar (nothing from the old header lost).
    char hdr[48];
    if(deauths >= DEAUTH_FLOOD_MIN) {
        // Attribution: name the most-attacked BSSID + channel (mutex held here).
        int top = -1;
        uint32_t topc = 0;
        for(size_t i = 0; i < app->deauth_count; i++) {
            if(app->deauth[i].count > topc) {
                topc = app->deauth[i].count;
                top = (int)i;
            }
        }
        if(top >= 0) {
            DeauthTarget* t = &app->deauth[top];
            snprintf(
                hdr,
                sizeof(hdr),
                "!DEAUTH ch%u %02X%02X%02X",
                t->channel,
                t->bssid[3],
                t->bssid[4],
                t->bssid[5]);
        } else {
            snprintf(
                hdr,
                sizeof(hdr),
                "%s DEAUTH! x%lu",
                connected ? "ESP" : "...",
                (unsigned long)deauths);
        }
    } else if(generic) {
        // Companion status counters stay 0 on a Marauder board; show the RX
        // line heartbeat and the detection count instead.
        snprintf(
            hdr,
            sizeof(hdr),
            "%s  rx %lu  hits %zu",
            connected ? "ESP" : "...",
            (unsigned long)lines,
            count);
    } else {
        snprintf(
            hdr,
            sizeof(hdr),
            "%s ch%u  seen %lu  hits %lu",
            connected ? "ESP" : "...",
            channel,
            (unsigned long)frames,
            (unsigned long)hits);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 22, hdr);

    char gps_str[12];
    if(app->settings.gps_enabled) {
        if(gps_valid) {
            snprintf(gps_str, sizeof(gps_str), "G:%d", gps_sats);
        } else {
            snprintf(gps_str, sizeof(gps_str), "G:-");
        }
        canvas_draw_str_aligned(canvas, 128, 22, AlignRight, AlignBottom, gps_str);
    }
    canvas_draw_line(canvas, 0, 24, 128, 24);

    if(count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            64,
            44,
            AlignCenter,
            AlignCenter,
            connected ? "Scanning for ALPR..." : "Connect ESP32...");
        furi_mutex_release(app->mutex);
        return;
    }

    // Clamp selection/scroll.
    if(model->selected >= (int)count) model->selected = count - 1;
    if(model->selected < 0) model->selected = 0;
    if(model->selected < model->top) model->top = model->selected;
    if(model->selected >= model->top + VISIBLE_ROWS)
        model->top = model->selected - VISIBLE_ROWS + 1;
    if(model->top < 0) model->top = 0;

    for(int row = 0; row < VISIBLE_ROWS; row++) {
        int idx = model->top + row;
        if(idx >= (int)count) break;
        FlockEntry* e = &app->flock[idx];

        int y = LIST_TOP + row * ROW_H;
        bool sel = (idx == model->selected);
        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y - 1, 128, ROW_H);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_set_color(canvas, ColorBlack);
        }

        char line[48];
        if(e->ssid[0] != '\0') {
            snprintf(line, sizeof(line), "%c %s", confidence_char(e->confidence), e->ssid);
        } else {
            snprintf(
                line,
                sizeof(line),
                "%c %02X:%02X:%02X:%02X:%02X:%02X",
                confidence_char(e->confidence),
                e->mac[0],
                e->mac[1],
                e->mac[2],
                e->mac[3],
                e->mac[4],
                e->mac[5]);
        }
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, y + 8, line);

        // Right edge: RSSI as signal bars (replaces the raw "-33dB"). The bars
        // helper hardcodes ColorBlack, so on the inverted (selected) row it
        // would be invisible -> show the exact dB as white text there instead.
        if(sel) {
            char meta[18];
            if(e->marked) {
                snprintf(meta, sizeof(meta), "*%ddB", e->rssi);
            } else {
                snprintf(meta, sizeof(meta), "%ddB", e->rssi);
            }
            canvas_draw_str_aligned(canvas, 126, y + 8, AlignRight, AlignBottom, meta);
        } else {
            if(e->marked) {
                // marked indicator just left of the bars
                canvas_draw_str(canvas, 96, y + 8, "*");
            }
            ui_signal_bars(canvas, 104, y - 1, e->rssi); // cell ~104..114, baseline y+7
        }
    }
    canvas_set_color(canvas, ColorBlack);

    furi_mutex_release(app->mutex);
}

static bool flock_view_input_callback(InputEvent* event, void* context) {
    FlockView* fv = context;
    bool handled = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                fv->view,
                FlockViewModel * model,
                {
                    if(model->selected > 0) model->selected--;
                },
                true);
            handled = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                fv->view,
                FlockViewModel * model,
                {
                    ReconApp* app = model->app;
                    int count = 0;
                    if(app) {
                        furi_mutex_acquire(app->mutex, FuriWaitForever);
                        count = (int)app->flock_count;
                        furi_mutex_release(app->mutex);
                    }
                    if(model->selected < count - 1) model->selected++;
                },
                true);
            handled = true;
        } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
            int sel = 0;
            with_view_model(fv->view, FlockViewModel * model, { sel = model->selected; }, false);
            if(fv->ok_cb) fv->ok_cb(fv->ok_ctx, sel);
            handled = true;
        }
    }
    return handled;
}

FlockView* flock_view_alloc(void) {
    FlockView* fv = malloc(sizeof(FlockView));
    fv->ok_cb = NULL;
    fv->ok_ctx = NULL;
    fv->view = view_alloc();
    view_set_context(fv->view, fv);
    view_allocate_model(fv->view, ViewModelTypeLocking, sizeof(FlockViewModel));
    view_set_draw_callback(fv->view, flock_view_draw_callback);
    view_set_input_callback(fv->view, flock_view_input_callback);
    with_view_model(
        fv->view,
        FlockViewModel * model,
        {
            model->app = NULL;
            model->selected = 0;
            model->top = 0;
        },
        false);
    return fv;
}

void flock_view_free(FlockView* fv) {
    furi_assert(fv);
    view_free(fv->view);
    free(fv);
}

View* flock_view_get_view(FlockView* fv) {
    furi_assert(fv);
    return fv->view;
}

void flock_view_set_app(FlockView* fv, void* app) {
    with_view_model(fv->view, FlockViewModel * model, { model->app = app; }, false);
}

void flock_view_set_ok_callback(FlockView* fv, FlockViewOkCallback cb, void* context) {
    fv->ok_cb = cb;
    fv->ok_ctx = context;
}

void flock_view_refresh(FlockView* fv) {
    with_view_model(fv->view, FlockViewModel * model, { UNUSED(model); }, true);
}

void flock_view_reset(FlockView* fv) {
    with_view_model(
        fv->view,
        FlockViewModel * model,
        {
            model->selected = 0;
            model->top = 0;
        },
        true);
}
