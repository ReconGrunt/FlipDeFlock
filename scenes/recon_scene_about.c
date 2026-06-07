#include "../recon_app_i.h"

#define RECON_ABOUT_TEXT                                                            \
    "FlipDeFlock\n"                                                                 \
    "Unified site-survey tool.\n \n"                                                \
    "FLOCK / ALPR DETECT\n"                                                         \
    "Finds Flock Safety / ALPR\n"                                                   \
    "cameras via an ESP32 board\n"                                                  \
    "(any board: Companion FW or\n"                                                 \
    "Marauder/generic). Matches\n"                                                  \
    "OUIs, phone-home probes &\n"                                                   \
    "SSID naming. OUI-only hits\n"                                                  \
    "are 'Possible' - verify by\n"                                                  \
    "eye, never assume.\n \n"                                                       \
    "Wiring: ESP32 on USART\n"                                                      \
    "(pin13 TX / pin14 RX).\n"                                                      \
    "GPS on LPUART (pin15/16)\n"                                                    \
    "so both run together.\n \n"                                                    \
    "NFC / RFID AUDIT\n"                                                            \
    "Identifies a card's protocol\n"                                                \
    "and grades its security for\n"                                                 \
    "access-control reviews.\n \n"                                                  \
    "REPORTS\n"                                                                     \
    "Marked finds export to\n"                                                      \
    "Markdown + DeFlock GeoJSON\n"                                                  \
    "under apps_data/\n"                                                            \
    "flipdeflock/reports.\n \n"                                                     \
    "Companion FW + OUI data:\n"                                                    \
    "see esp32_companion/ and\n"                                                    \
    "deflock.me. Passive recon,\n"                                                  \
    "lawful authorized use only."

void recon_scene_about_on_enter(void* context) {
    ReconApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_text_scroll_element(widget, 0, 0, 128, 64, RECON_ABOUT_TEXT);
    view_dispatcher_switch_to_view(app->view_dispatcher, ReconViewWidget);
}

bool recon_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void recon_scene_about_on_exit(void* context) {
    ReconApp* app = context;
    widget_reset(app->widget);
}
