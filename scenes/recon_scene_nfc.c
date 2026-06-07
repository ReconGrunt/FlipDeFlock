#include "../recon_app_i.h"
#include "../helpers/recon_nfc.h"
#include "../helpers/recon_report.h"

#include <math.h>

typedef enum {
    NfcCustomLog = 300,
} NfcCustomEvent;

static void recon_scene_nfc_button_cb(GuiButtonType type, InputType input, void* context) {
    ReconApp* app = context;
    if(input == InputTypeShort && type == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, NfcCustomLog);
    }
}

static void recon_scene_nfc_render(ReconApp* app, bool detected, FuriString* title, FuriString* grade, FuriString* detail) {
    Widget* widget = app->widget;
    widget_reset(widget);

    if(!detected) {
        widget_add_string_multiline_element(
            widget,
            64,
            28,
            AlignCenter,
            AlignCenter,
            FontSecondary,
            "Present a card to the\nback of the Flipper...");
        return;
    }

    widget_add_string_element(
        widget, 0, 2, AlignLeft, AlignTop, FontPrimary, furi_string_get_cstr(title));
    widget_add_string_element(
        widget, 127, 2, AlignRight, AlignTop, FontPrimary, furi_string_get_cstr(grade));
    widget_add_text_scroll_element(widget, 0, 16, 128, 30, furi_string_get_cstr(detail));
    widget_add_button_element(
        widget, GuiButtonTypeCenter, "Log", recon_scene_nfc_button_cb, app);
}

void recon_scene_nfc_on_enter(void* context) {
    ReconApp* app = context;
    app->nfc = recon_nfc_alloc(app);
    recon_nfc_start(app->nfc);
    app->text_store[0] = '\0';

    FuriString* dummy = furi_string_alloc();
    recon_scene_nfc_render(app, false, dummy, dummy, dummy);
    furi_string_free(dummy);

    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

bool recon_scene_nfc_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        FuriString* title = furi_string_alloc();
        FuriString* grade = furi_string_alloc();
        FuriString* detail = furi_string_alloc();
        bool detected = recon_nfc_get(app->nfc, title, grade, detail);

        // Re-render only when the displayed card/grade changes (avoids flicker).
        char sig[64];
        if(detected) {
            snprintf(
                sig,
                sizeof(sig),
                "%s|%s",
                furi_string_get_cstr(title),
                furi_string_get_cstr(grade));
        } else {
            sig[0] = '\0';
        }
        if(strncmp(sig, app->text_store, sizeof(sig)) != 0) {
            strncpy(app->text_store, sig, RECON_TEXT_STORE - 1);
            app->text_store[RECON_TEXT_STORE - 1] = '\0';
            recon_scene_nfc_render(app, detected, title, grade, detail);
        }

        furi_string_free(title);
        furi_string_free(grade);
        furi_string_free(detail);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom && event.event == NfcCustomLog) {
        FuriString* title = furi_string_alloc();
        FuriString* grade = furi_string_alloc();
        FuriString* detail = furi_string_alloc();
        if(recon_nfc_get(app->nfc, title, grade, detail)) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            bool fix = app->gps_valid;
            float lat = app->gps_lat, lon = app->gps_lon;
            furi_mutex_release(app->mutex);

            FuriString* line = furi_string_alloc();
            if(fix) {
                furi_string_printf(
                    line,
                    "%s,%s,%.6f,%.6f",
                    furi_string_get_cstr(title),
                    furi_string_get_cstr(grade),
                    (double)lat,
                    (double)lon);
            } else {
                furi_string_printf(
                    line, "%s,%s,,", furi_string_get_cstr(title), furi_string_get_cstr(grade));
            }
            bool ok = recon_report_append_nfc(app, furi_string_get_cstr(line));
            furi_string_free(line);

            if(app->settings.sound) {
                notification_message(
                    app->notifications, ok ? &sequence_success : &sequence_error);
            }
        }
        furi_string_free(title);
        furi_string_free(grade);
        furi_string_free(detail);
        consumed = true;
    }
    return consumed;
}

void recon_scene_nfc_on_exit(void* context) {
    ReconApp* app = context;
    if(app->nfc) {
        recon_nfc_stop(app->nfc);
        recon_nfc_free(app->nfc);
        app->nfc = NULL;
    }
    widget_reset(app->widget);
}
