#pragma once

/*
 * In-app ESP32 flasher: connect (download mode), flash a .bin, and back up the
 * current firmware to SD. Built on Espressif's esp-serial-flasher (Apache-2.0,
 * vendored under lib/esp-serial-flasher). This port + worker is original (MIT).
 *
 * Bootloader entry is MANUAL: the user puts the ESP32 into download/bootloader
 * mode (hold BOOT, tap RESET) before connecting. reset/enter-bootloader port
 * hooks are no-ops (board-agnostic).
 */

#include <furi_hal_serial.h>
#include <storage/storage.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct EspFlasher EspFlasher;

/** Log/progress sink. `line` is a NUL-terminated message (no trailing newline). */
typedef void (*EspFlasherLog)(void* ctx, const char* line);

/** Acquire the UART (disables the expansion module). NULL on failure. */
EspFlasher* esp_flasher_alloc(FuriHalSerialId ch, EspFlasherLog log_cb, void* ctx);
void esp_flasher_free(EspFlasher* f);

/**
 * Sync with the target in download mode. Retries the SYNC several times.
 *
 * @param use_stub  true: upload the flasher stub (needed for BACKUP/read).
 *                  false: talk to the raw ROM loader (FLASHING/write), like the
 *                  0xchocolate ESP Flasher -- lighter and avoids the stub's
 *                  MD5-checked transfer.
 * @param fast_baud non-zero: raise the link to that rate after connecting; on
 *                  failure the connection is aborted, so use Safe (0) instead.
 */
bool esp_flasher_connect(EspFlasher* f, uint32_t fast_baud, bool use_stub);

/** Flash `path` to the target at `addr` (use 0 for a merged full image). */
bool esp_flasher_flash_file(EspFlasher* f, Storage* storage, const char* path, uint32_t addr);

/** Dump the whole target flash to `out_path` (full backup). */
bool esp_flasher_backup(EspFlasher* f, Storage* storage, const char* out_path);

/** Request the in-progress flash/backup loop to stop ASAP (e.g. user backs out). */
void esp_flasher_abort(void);
