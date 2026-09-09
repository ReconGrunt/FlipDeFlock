// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// Everything probing nearby, ranked, WHETHER OR NOT it matched anything we ship.
//
// WHY THIS SCREEN EXISTS (issue #25). The detector only ever reports what it
// already recognises. Every modern Flock camera randomises its MAC, so it has no
// vendor prefix to match and it scores nothing -- and recon_app.c drops anything
// scoring FlockConfidenceNone before it reaches the hit table. The result, from a
// real report: a reporter drove past ten-plus cameras and the app stayed empty
// the whole time, which is indistinguishable from a broken board.
//
// The survey was already being collected and written to survey.csv. It was just
// never shown on the device, so the one thing that could have broken the deadlock
// was legible only afterwards, on a computer, to someone who knew what to look
// for. That is what this fixes: same data, on the screen, ranked the way a person
// reads it.
//
// AND IT IS THE ONLY WAY TO TEACH THE APP A RANDOMISED CAMERA. "Confirm: I saw
// it" lives on the detection list, which by definition a randomised camera never
// reaches -- so the self-learning added in v0.91 could not be pointed at the
// devices it was built for. Learning from a survey row closes that loop.
//
// NOT A DETECTION LIST. Nothing here enters the hit table, nothing is exported as
// a sighting, and no row claims to be a camera. A high rank means "this behaves
// the way a fixed installation behaves", which a busy access point also does. The
// operator supplies the ground truth by looking with their eyes.
#include "../recon_app_i.h"
#include "../helpers/survey_rank.h"

// Ranked view of app->survey[], rebuilt on entry. Static rather than on the
// stack: RECON_SURVEY_MAX entries is more than a scene handler's stack should
// carry, and this screen is never re-entrant.
static SurveyRanked g_ranked[RECON_SURVEY_MAX];
static int g_count;

static void recon_scene_survey_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void recon_scene_survey_on_enter(void* context) {
    ReconApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    SurveyRankRow rows[RECON_SURVEY_MAX];
    size_t n = 0;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    n = app->survey_count;
    if(n > RECON_SURVEY_MAX) n = RECON_SURVEY_MAX;
    for(size_t i = 0; i < n; i++) {
        memcpy(rows[i].mac, app->survey[i].mac, 6);
        rows[i].fp = app->survey[i].fp;
        rows[i].rssi = app->survey[i].rssi;
        rows[i].channel = app->survey[i].channel;
        rows[i].count = app->survey[i].count;
    }
    furi_mutex_release(app->mutex);

    g_count = (int)survey_rank(rows, n, g_ranked, RECON_SURVEY_MAX);

    // Say what the list IS, in the one line always on screen. "Air survey" and
    // not "detections", because the difference is the entire point.
    submenu_set_header(submenu, g_count ? "Air survey (not hits)" : "Air survey");

    char row[32];
    for(int i = 0; i < g_count; i++) {
        const SurveyRankRow* r = &rows[g_ranked[i].index];
        uint8_t ev = g_ranked[i].evidence;

        // Two markers, both about why a MAC is or is not useful here:
        //   ~  randomised address -- no vendor prefix, so no OUI can ever match
        //   g  commodity scan skeleton -- shared with phones, tells us nothing
        char flags[4];
        int f = 0;
        if(ev & SurveyEvidenceLocalAdmin) flags[f++] = '~';
        if(ev & SurveyEvidenceGeneric) flags[f++] = 'g';
        flags[f] = '\0';

        // Signal, persistence, then the OUI half of the address -- the three
        // things that separated the camera from its neighbours in #25, in the
        // order a person compares them.
        snprintf(
            row,
            sizeof(row),
            "%d x%u %02X:%02X:%02X%s%s",
            (int)r->rssi,
            (unsigned)r->count,
            r->mac[0],
            r->mac[1],
            r->mac[2],
            f ? " " : "",
            flags);
        submenu_add_item(submenu, row, (uint32_t)i, recon_scene_survey_cb, app);
    }

    if(!g_count) {
        // An empty survey is a real answer and has to read like one. "Nothing is
        // probing" and "the board is dead" look identical on a blank screen, and
        // telling those apart is exactly what issue #25 was.
        submenu_add_item(submenu, "Nothing probing yet", 0, recon_scene_survey_cb, app);
        submenu_add_item(submenu, "Run a scan first", 1, recon_scene_survey_cb, app);
    }

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, ReconSceneSurvey));
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_survey_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(!g_count) return true; // the placeholder rows do nothing

    int i = (int)event.event;
    if(i < 0 || i >= g_count) return true;

    scene_manager_set_scene_state(app->scene_manager, ReconSceneSurvey, event.event);
    app->survey_menu_idx = (int)g_ranked[i].index;
    scene_manager_next_scene(app->scene_manager, ReconSceneSurveyDetail);
    return true;
}

void recon_scene_survey_on_exit(void* context) {
    ReconApp* app = context;
    submenu_reset(app->submenu);
}
