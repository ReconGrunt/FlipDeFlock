#include "../recon_app_i.h"

typedef enum {
    StartItemFlock,
    StartItemWifi,
    StartItemBle,
    StartItemNfc,
    StartItemFirmware,
    StartItemReports,
    StartItemSettings,
    StartItemAbout,
} StartItem;

static void recon_scene_start_submenu_cb(void* context, uint32_t index) {
    ReconApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void recon_scene_start_on_enter(void* context) {
    ReconApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "FlipDeFlock");
    submenu_add_item(
        submenu, "Flock / ALPR Detect", StartItemFlock, recon_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "WiFi Audit", StartItemWifi, recon_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "BLE / Tracker Scan", StartItemBle, recon_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "NFC / RFID Audit", StartItemNfc, recon_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "ESP32 Firmware", StartItemFirmware, recon_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Reports", StartItemReports, recon_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Settings", StartItemSettings, recon_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartItemAbout, recon_scene_start_submenu_cb, app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, ReconSceneStart));
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewSubmenu);
}

bool recon_scene_start_on_event(void* context, SceneManagerEvent event) {
    ReconApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, ReconSceneStart, event.event);
        consumed = true;
        switch(event.event) {
        case StartItemFlock:
            scene_manager_next_scene(app->scene_manager, ReconSceneFlock);
            break;
        case StartItemWifi:
            scene_manager_next_scene(app->scene_manager, ReconSceneWifi);
            break;
        case StartItemBle:
            scene_manager_next_scene(app->scene_manager, ReconSceneBle);
            break;
        case StartItemNfc:
            scene_manager_next_scene(app->scene_manager, ReconSceneNfc);
            break;
        case StartItemFirmware:
            scene_manager_next_scene(app->scene_manager, ReconSceneFirmware);
            break;
        case StartItemReports:
            scene_manager_next_scene(app->scene_manager, ReconSceneReports);
            break;
        case StartItemSettings:
            scene_manager_next_scene(app->scene_manager, ReconSceneSettings);
            break;
        case StartItemAbout:
            scene_manager_next_scene(app->scene_manager, ReconSceneAbout);
            break;
        default:
            consumed = false;
            break;
        }
    }
    return consumed;
}

void recon_scene_start_on_exit(void* context) {
    ReconApp* app = context;
    submenu_reset(app->submenu);
}
