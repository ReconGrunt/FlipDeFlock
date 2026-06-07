#pragma once

/**
 * ESP32 link: drives any ESP32 board over UART and feeds Flock detections into
 * the owning ReconApp.
 *
 * Two backends (selectable in settings):
 *  - Companion: our flock_companion firmware, strict "D,"/"S," line protocol.
 *  - Generic:   Marauder or any firmware -- scrapes MAC and SSID tokens out of
 *               whatever text the board emits and applies the Flock filter
 *               locally. Universal fallback that needs no specific firmware.
 */

typedef struct EspLink EspLink;

/** @param app  ReconApp* (void* to avoid a header cycle). */
EspLink* esp_link_alloc(void* app);
void esp_link_free(EspLink* esp);

/** Disable expansion, acquire the configured serial port, start the worker. */
void esp_link_start(EspLink* esp);
/** Stop the worker, release the port, re-enable expansion. */
void esp_link_stop(EspLink* esp);

/** Send a raw command line (newline appended automatically). */
void esp_link_send(EspLink* esp, const char* cmd);
