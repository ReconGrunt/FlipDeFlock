// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// One surveyed transmitter, and the one action worth offering on it: teach the
// app its probe fingerprint after looking at the thing with your own eyes.
//
// WHY LEARNING HAPPENS HERE. A fingerprint hashes the SHAPE of a probe request,
// not the address, so it still matches after a device rotates its MAC -- which is
// the only handle anyone has on a modern Flock camera. v0.91 added that learning
// step, but hung it off the detection list, and a randomised camera never reaches
// the detection list (recon_app.c drops FlockConfidenceNone). So the feature
// could not be aimed at the devices it was built for. This screen is where it can
// be.
//
// WHAT A LEARNED FINGERPRINT IS WORTH. It is capped at "Class?" and can never
// reach Confirmed on its own, because the operator's eyes are good evidence about
// a camera and no evidence at all about which row in a list produced which
// packet. Getting it wrong costs a weak lead, not a false camera -- and a
// commodity scan skeleton is refused outright, so the most likely mistake cannot
// be made at all.
#include "../recon_app_i.h"
#include "../helpers/survey_rank.h"
#include "../helpers/sig_db.h"
#include "../helpers/flock_db.h"

#include <gui/modules/widget.h>

typedef enum {
    SurveyDetailLearn = 0,
    SurveyDetailPin,
} SurveyDetailEvent;

// What happened on the last button press, so the redraw can report it. A screen
// that looks identical after an action leaves the operator unsure it registered.
typedef enum {
    SurveyLearnIdle = 0,
    SurveyLearnOk,
    SurveyLearnGeneric, // refused: commodity skeleton, would flag phones
    SurveyLearnNoFp, // nothing to learn: no fingerprint was captured
    SurveyLearnFailed, // already known, list full, or the card refused it
    SurveyPinOk,
    SurveyPinFailed, // already pinned, list full, or the card refused it
} SurveyLearnState;

static SurveyLearnState g_state;

static void recon_scene_survey_detail_button_cb(GuiButtonType type, InputType input, void* ctx) {
    ReconApp* app = ctx;
    if(input != InputTypeShort) return;
    if(type == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SurveyDetailLearn);
    } else if(type == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, SurveyDetailPin);
    }
}

/** Copy the selected row out from under the lock. False if it is gone. */
static bool survey_detail_row(ReconApp* app, SurveyEntry* out) {
    bool ok = false;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->survey_menu_idx >= 0 && app->survey_menu_idx < (int)app->survey_count) {
        *out = app->survey[app->survey_menu_idx];
        ok = true;
    }
    furi_mutex_release(app->mutex);
    return ok;
}

static void recon_scene_survey_detail_draw(ReconApp* app) {
    Widget* widget = app->widget;
    widget_reset(widget);

    SurveyEntry e;
    if(!survey_detail_row(app, &e)) {
        widget_add_string_element(
            widget, 64, 28, AlignCenter, AlignCenter, FontSecondary, "Row is gone");
        return;
    }

    FuriString* s = furi_string_alloc();

    // THE RESULT GOES FIRST, ABOVE THE FOLD.
    //
    // It used to be appended after the address, RSSI, fingerprint and address
    // kind -- six lines into a scroll element four lines tall. Pressing "I saw
    // it" on the bench therefore changed nothing visible at all: the screen
    // still had to be scrolled down two lines to find "Learned." That is the
    // exact failure this state machine was added to prevent, arriving from the
    // one direction nobody checked -- where on the screen the answer landed.
    switch(g_state) {
    case SurveyLearnOk:
        furi_string_cat_str(s, "Learned. Restart the app\nto use it.\n \n");
        break;
    case SurveyLearnGeneric:
        furi_string_cat_str(s, "Not learned: too common,\nit would flag phones.\n \n");
        break;
    case SurveyLearnNoFp:
        furi_string_cat_str(s, "Nothing to learn: no\nfingerprint captured.\n \n");
        break;
    case SurveyLearnFailed:
        furi_string_cat_str(s, "Not learned: already\nknown, or list full.\n \n");
        break;
    case SurveyPinOk:
        furi_string_cat_str(s, "Address pinned. Restart\nthe app to use it.\n \n");
        break;
    case SurveyPinFailed:
        furi_string_cat_str(s, "Not pinned: already\npinned, or list full.\n \n");
        break;
    case SurveyLearnIdle:
    default:
        break;
    }

    furi_string_cat_printf(
        s,
        "%02X:%02X:%02X:%02X:%02X:%02X\n",
        e.mac[0],
        e.mac[1],
        e.mac[2],
        e.mac[3],
        e.mac[4],
        e.mac[5]);
    furi_string_cat_printf(s, "%d dBm  ch %u  x%u\n", (int)e.rssi, e.channel, (unsigned)e.count);

    if(e.fp) {
        furi_string_cat_printf(s, "IE-fp: %08lx\n", (unsigned long)e.fp);
    } else {
        furi_string_cat_str(s, "IE-fp: none captured\n");
    }

    // The address kind, spelled out. "Randomised" is the answer to the question
    // this whole issue was about -- why a camera standing in front of you matches
    // no vendor table.
    furi_string_cat_str(
        s, survey_mac_is_local(e.mac) ? "Addr: randomised\n" : "Addr: vendor OUI\n");

    if(flock_ie_fp_is_generic(e.fp)) {
        furi_string_cat_str(s, "Common scan pattern -\nshared with phones.\n");
    }

    // x=2/y=1, not 0/0: at the origin the scroll element puts the first
    // baseline on row 0 and shaves the top pixel row off every glyph, and the
    // leftmost column sits flush against the bezel.
    widget_add_text_scroll_element(widget, 2, 1, 124, 51, furi_string_get_cstr(s));
    furi_string_free(s);

    // Offered only when there is something learnable. A button that always
    // refuses teaches the operator to ignore buttons.
    if(e.fp && !flock_ie_fp_is_generic(e.fp)) {
        widget_add_button_element(
            widget, GuiButtonTypeCenter, "I saw it", recon_scene_survey_detail_button_cb, app);
    }
    // PIN THE ADDRESS, offered whatever the fingerprint says.
    //
    // A randomised MAC is not necessarily a rotating one: the first camera
    // anyone checked twice kept the identical invented address across visits
    // days apart. Nothing else in the app can name that unit, because there is
    // no vendor behind the address for an OUI table to match and three bytes of
    // it is a prefix shared with whatever else randomises into it.
    //
    // Offered even when the fingerprint is a commodity one, since the address is
    // then the ONLY handle left.
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Pin addr", recon_scene_survey_detail_button_cb, app);
}

void recon_scene_survey_detail_on_enter(void* context) {
    ReconApp* app = context;
    g_state = SurveyLearnIdle;
    recon_scene_survey_detail_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

bool recon_scene_survey_detail_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != SurveyDetailLearn && event.event != SurveyDetailPin) return false;

    SurveyEntry e;
    if(event.event == SurveyDetailPin) {
        if(!survey_detail_row(app, &e)) {
            g_state = SurveyPinFailed;
        } else {
            g_state = sig_db_learn_mac(app->storage, e.mac) ? SurveyPinOk : SurveyPinFailed;
        }
        recon_scene_survey_detail_draw(app);
        return true;
    }

    if(!survey_detail_row(app, &e)) {
        g_state = SurveyLearnFailed;
    } else if(!e.fp) {
        g_state = SurveyLearnNoFp;
    } else if(flock_ie_fp_is_generic(e.fp)) {
        // Belt and braces: the button is not drawn for these, and sig_db_learn_fp
        // refuses them anyway. Both guards stay -- this is the mistake that
        // poisons a card, and it is silent when it happens.
        g_state = SurveyLearnGeneric;
    } else {
        g_state = sig_db_learn_fp(app->storage, e.fp) ? SurveyLearnOk : SurveyLearnFailed;
    }

    recon_scene_survey_detail_draw(app);
    return true;
}

void recon_scene_survey_detail_on_exit(void* context) {
    ReconApp* app = context;
    widget_reset(app->widget);
}
