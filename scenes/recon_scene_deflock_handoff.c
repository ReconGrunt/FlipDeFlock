// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "../recon_app_i.h"
#include "../helpers/plugin_host.h"
#include "../helpers/deflock_url.h"
#include "../plugins/qr_plugin_api.h"

#include <math.h>
#include <stdlib.h> // malloc/free for the scene-scoped snapshot

// Snapshot of the marked, geotagged cameras taken on_enter. This scene is
// passive: it starts no ESP/GPS link and holds no UART -- it only renders a
// QR/URL the user scans with their phone. The list is a copy so the draw path
// never touches the live flock[] under app->mutex.
#define HANDOFF_MAX RECON_FLOCK_MAX

typedef struct {
    float lat;
    float lon;
    float heading;
    FlockConfidence confidence;
    FlockDevClass dev_class;
    FlockVendor vendor;
} HandoffCam;

/**
 * Scene-scoped snapshot, allocated on entry and freed on exit. Was a static
 * array costing 1024 bytes of BSS for the whole app run. NULL degrades to
 * "no cameras", which is already a rendered state (see the g_cam_count == 0
 * branch), so there is no new failure mode to handle.
 */
static HandoffCam* g_cams;
static int g_cam_count;
static int g_selected;

// Forward the QR view's Left/Right paging into the scene as a custom event that
// carries the signed delta (-1/+1). Casting to uint32_t and back round-trips.
static void recon_scene_deflock_handoff_page_cb(void* context, int delta) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)delta);
}

// Build the per-camera handoff content and push it to the QR view. The URL lands
// the user on the DeFlock map at the camera so they can add it in the official
// app; the on-screen text mirrors the OSM/DeFlock tagging from recon_report.c.
static void recon_scene_deflock_handoff_show(ReconApp* app) {
    if(g_cam_count == 0) {
        deflock_qr_view_set_empty(app->deflock_qr_view);
        return;
    }
    if(g_selected < 0) g_selected = 0;
    if(g_selected >= g_cam_count) g_selected = g_cam_count - 1;

    HandoffCam* c = &g_cams[g_selected];

    // THE MAP IS ON ITS OWN SUBDOMAIN, and the zoom is not optional. Both facts,
    // the worst-case buffer size, and the reason the apex domain is wrong now
    // live in helpers/deflock_url.h -- where test_deflock_url.c can assert them,
    // rather than in a scene where the only check available was to scan the QR
    // off the screen with a phone. That is why issue #25 survived four releases.
    char url[DEFLOCK_URL_LEN];
    deflock_map_url(url, sizeof(url), (double)c->lat, (double)c->lon);

    // RIGHT COLUMN -- two SHORT lines. It is 72 px wide, about fourteen
    // characters, and the full coordinates used to be drawn here: they ran off
    // the edge as "40.712799,-74.0", losing the longitude on the one screen
    // whose job is to hand over a position. They now live in the full-width
    // strip below, which fits them.
    char who[28];
    snprintf(
        who,
        sizeof(who),
        "%s",
        (c->vendor != FlockVendorUnknown) ? flock_vendor_str(c->vendor) :
                                            flock_class_str(c->dev_class));

    char conf[16];
    snprintf(conf, sizeof(conf), "%s", flock_confidence_str(c->confidence));

    // BOTTOM STRIP: the coordinates, then the OSM tagging to copy into DeFlock.
    //
    // THE TAGS USED TO BE HARDCODED, claiming surveillance:type=ALPR and
    // manufacturer="Flock Safety" about whatever happened to be marked.
    // recon_report.c carried the identical bug in the export path and was fixed;
    // this screen kept it, so a Ubicquia streetlight, an Axon pole or a hit on a
    // MAC in no vendor table at all was handed to the operator as a Flock ALPR
    // camera to type into a public map. Same rule as the exporter: state the
    // class actually determined, and name a manufacturer only when a vendor
    // table matched. Drones never reach here at all (see the snapshot loop).
    //
    // direction= is gone from this screen: the strip holds three lines and the
    // coordinates had to have one of them. It is still written to every export,
    // which is what anyone actually submits from.
    char tags[96];
    int tw = snprintf(tags, sizeof(tags), "%.6f,%.6f\n", (double)c->lat, (double)c->lon);
    if(tw < 0) tw = 0;
    if((size_t)tw >= sizeof(tags)) tw = (int)sizeof(tags) - 1;
    snprintf(
        tags + tw,
        sizeof(tags) - (size_t)tw,
        "man_made=surveillance\nsurveillance:type=%s",
        (c->dev_class == FlockClassAcoustic) ? "acoustic" :
        (c->dev_class == FlockClassBodycam)  ? "camera" :
        (c->dev_class == FlockClassGear)     ? "unknown" :
                                               "ALPR");

    deflock_qr_view_set_content(
        app->deflock_qr_view, url, g_selected, g_cam_count, who, conf, tags);
}

/** QR encoder plugin, mapped in only while this screen is open. NULL when it
 *  could not be loaded -- the view then shows its "QR n/a" fallback. */
static PluginHost* g_qr_plugin = NULL;

void recon_scene_deflock_handoff_on_enter(void* context) {
    ReconApp* app = context;

    // Allocate the snapshot list. v0.48 (d0a12a3) moved this array off BSS to a
    // heap pointer in all three scenes that carried one, but only added the
    // allocation to the other two -- so g_cams was NULL on every entry here and
    // the `g_cams &&` guard below silently collected nothing. Share to DeFlock
    // reported "No marked cameras" no matter what was marked, from v0.48 to
    // v0.50. Same shape as recon_scene_guardian_sus.c and recon_scene_locator.c.
    if(!g_cams) g_cams = malloc(sizeof(HandoffCam) * HANDOFF_MAX);

    // Snapshot marked + geotagged cameras under the mutex into the local list.
    g_cam_count = 0;
    g_selected = 0;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(size_t i = 0; g_cams && i < app->flock_count && g_cam_count < HANDOFF_MAX; i++) {
        FlockEntry* e = &app->flock[i];
        // A DRONE IS NOT A PLACE. Its Remote ID broadcast carries a position, so
        // it passed the geotag test and was offered here like any camera -- with
        // a QR pointing DeFlock at a spot an aircraft flew over minutes ago, and
        // OSM tags calling it permanent surveillance. The exporter already
        // refuses to tag one; this screen is the other way the same coordinate
        // reaches a public map, and it has to refuse too.
        if(e->dev_class == FlockClassDrone) continue;
        if(e->marked && !isnan(e->lat) && !isnan(e->lon)) {
            HandoffCam* c = &g_cams[g_cam_count++];
            c->lat = e->lat;
            c->lon = e->lon;
            c->heading = e->heading;
            c->confidence = e->confidence;
            c->dev_class = (FlockDevClass)e->dev_class;
            c->vendor = flock_vendor_of(e->mac, e->ssid);
        }
    }
    furi_mutex_release(app->mutex);

    // Map the encoder in for the lifetime of this screen. A failure here is
    // not fatal: set_api(NULL) makes the view draw the text fallback, and the
    // coordinates are still readable and hand-enterable at deflock.org/report.
    const QrPluginApi* qr_api = NULL;
    g_qr_plugin = plugin_host_load(QR_PLUGIN_APP_ID, QR_PLUGIN_API_VERSION, (const void**)&qr_api);
    deflock_qr_view_set_api(app->deflock_qr_view, qr_api);

    deflock_qr_view_set_page_callback(
        app->deflock_qr_view, recon_scene_deflock_handoff_page_cb, app);

    recon_scene_deflock_handoff_show(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewDeflockQr);
}

bool recon_scene_deflock_handoff_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        // Left/Right paging arrives as a custom event carrying the delta (+1/-1).
        if(g_cam_count > 0) {
            int next = g_selected + (int)event.event;
            if(next < 0) next = g_cam_count - 1;
            if(next >= g_cam_count) next = 0;
            g_selected = next;
            recon_scene_deflock_handoff_show(app);
        }
        consumed = true;
    }
    return consumed;
}

void recon_scene_deflock_handoff_on_exit(void* context) {
    ReconApp* app = context;
    // Drop the borrowed pointer BEFORE unmapping the plugin it points into, so
    // a later redraw of a stale model can never call through freed code.
    deflock_qr_view_set_api(app->deflock_qr_view, NULL);
    plugin_host_free(g_qr_plugin);
    g_qr_plugin = NULL;

    free(g_cams);
    g_cams = NULL;
    g_cam_count = 0;
    g_selected = 0;
}
