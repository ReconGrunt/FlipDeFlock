// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
/**
 * @file plugin_host.h
 * Load/unload a FlipDeFlock plugin from the app's own asset directory.
 *
 * A plugin ships as a .fal embedded in the .fap and is mapped into RAM only
 * while the screen that needs it is open. That is what keeps it out of the
 * contiguous allocation the loader must find at launch -- the failure users
 * were hitting (issue #5).
 *
 * ONLY THE QR ENCODER USES THIS TODAY (`flipdeflock_qr`, ~8.7 KB out of the
 * image). This comment previously also named the ESP32 flasher, which was
 * aspirational rather than true: `application.fam` declares one PLUGIN App(),
 * and `esp_loader_*` / `esp_flasher_*` are linked straight into the .fap.
 *
 * The flasher is by far the biggest candidate left -- measured at ~27.6 KB,
 * about 26% of the app's code and constants, and reached only from the firmware
 * screen. Moving it here is worth roughly four times what pulling Net Guardian
 * and the Wi-Fi audit out would save (~6.3 KB combined). Do not re-add a claim
 * that it already lives here without checking `nm` on the built .elf.
 *
 * FAIL-SAFE BY CONTRACT: every function here returns NULL/void on any problem
 * -- missing asset directory, version mismatch, corrupt .fal -- and never
 * traps. A caller that gets NULL must show "unavailable" and let the user back
 * out. The asset directory can legitimately be missing (a user who copied the
 * .fap onto a card the firmware has not extracted assets for yet), so treating
 * that as fatal would turn a cosmetic problem into a dead app.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PluginHost PluginHost;

/**
 * Load the single plugin matching `app_id`/`api_version` from the app's assets.
 *
 * @param app_id       plugin appid, e.g. QR_PLUGIN_APP_ID
 * @param api_version  ABI version the caller was compiled against
 * @param out_api      receives the plugin's API struct pointer on success
 * @return             handle to free with plugin_host_free(), or NULL on any
 *                     failure (in which case *out_api is left NULL)
 */
PluginHost* plugin_host_load(const char* app_id, uint32_t api_version, const void** out_api);

/** Unload and free. NULL-safe. Invalidates the API pointer from load(). */
void plugin_host_free(PluginHost* host);

#ifdef __cplusplus
}
#endif
