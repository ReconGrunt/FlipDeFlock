#include "esp_flasher.h"

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <string.h>
#include <stdarg.h>

#include <esp_loader.h>
#include <esp_loader_io.h>

#define FLASH_BAUD 115200
#define FLASH_RX_BUF 4096
#define FLASH_BLOCK 1024 /* flash_write payload */
#define BACKUP_CHUNK 2048 /* flash_read chunk */

struct EspFlasher {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    EspFlasherLog log;
    void* ctx;
};

/* The esp-serial-flasher port hooks are global free functions, so the active
 * instance + timer deadline live in file statics. One flasher at a time. */
static EspFlasher* s_active = NULL;
static uint32_t s_deadline = 0;
static volatile bool s_abort = false;

void esp_flasher_abort(void) {
    s_abort = true;
}

static void esp_flasher_logf(EspFlasher* f, const char* fmt, ...) {
    if(!f || !f->log) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f->log(f->ctx, buf);
}

/* ---- esp-serial-flasher port (loader_port_*) ---- */

esp_loader_error_t loader_port_read(uint8_t* data, uint16_t size, uint32_t timeout) {
    if(!s_active) return ESP_LOADER_ERROR_FAIL;
    size_t got = furi_stream_buffer_receive(s_active->rx, data, size, timeout);
    return (got < size) ? ESP_LOADER_ERROR_TIMEOUT : ESP_LOADER_SUCCESS;
}

esp_loader_error_t loader_port_write(const uint8_t* data, uint16_t size, uint32_t timeout) {
    UNUSED(timeout);
    if(!s_active) return ESP_LOADER_ERROR_FAIL;
    furi_hal_serial_tx(s_active->serial, data, size);
    return ESP_LOADER_SUCCESS;
}

void loader_port_delay_ms(uint32_t ms) {
    furi_delay_ms(ms);
}

void loader_port_start_timer(uint32_t ms) {
    s_deadline = furi_get_tick() + ms;
}

uint32_t loader_port_remaining_time(void) {
    uint32_t now = furi_get_tick();
    return (s_deadline > now) ? (s_deadline - now) : 0;
}

/* Manual bootloader entry: the user resets the board into download mode. */
void loader_port_reset_target(void) {
}

void loader_port_enter_bootloader(void) {
}

esp_loader_error_t loader_port_change_transmission_rate(uint32_t rate) {
    if(s_active) furi_hal_serial_set_br(s_active->serial, rate);
    return ESP_LOADER_SUCCESS;
}

void loader_port_debug_print(const char* str) {
    if(s_active && s_active->log) s_active->log(s_active->ctx, str);
}

void loader_port_spi_set_cs(uint32_t level) {
    UNUSED(level);
}

/* ---- UART RX into the stream buffer ---- */

static void esp_flasher_rx_irq(FuriHalSerialHandle* handle, FuriHalSerialRxEvent ev, void* ctx) {
    EspFlasher* f = ctx;
    if(ev == FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(f->rx, &b, 1, 0);
    }
}

EspFlasher* esp_flasher_alloc(FuriHalSerialId ch, EspFlasherLog log_cb, void* ctx) {
    EspFlasher* f = malloc(sizeof(EspFlasher));
    f->log = log_cb;
    f->ctx = ctx;
    f->rx = furi_stream_buffer_alloc(FLASH_RX_BUF, 1);

    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);

    f->serial = furi_hal_serial_control_acquire(ch);
    if(!f->serial) {
        furi_stream_buffer_free(f->rx);
        free(f);
        Expansion* exp = furi_record_open(RECORD_EXPANSION);
        expansion_enable(exp);
        furi_record_close(RECORD_EXPANSION);
        return NULL;
    }
    furi_hal_serial_init(f->serial, FLASH_BAUD);
    furi_hal_serial_async_rx_start(f->serial, esp_flasher_rx_irq, f, false);

    s_abort = false;
    s_active = f;
    return f;
}

void esp_flasher_free(EspFlasher* f) {
    if(!f) return;
    s_active = NULL;
    furi_hal_serial_async_rx_stop(f->serial);
    furi_hal_serial_deinit(f->serial);
    furi_hal_serial_control_release(f->serial);
    furi_stream_buffer_free(f->rx);
    free(f);

    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
}

bool esp_flasher_connect(EspFlasher* f, uint32_t fast_baud) {
    esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
    esp_flasher_logf(f, "Connecting (download mode)...");
    esp_loader_error_t err = esp_loader_connect_with_stub(&args);
    if(err != ESP_LOADER_SUCCESS) {
        esp_flasher_logf(f, "Connect failed (%d).", (int)err);
        esp_flasher_logf(f, "Put ESP in bootloader mode,");
        esp_flasher_logf(f, "then retry.");
        return false;
    }
    esp_flasher_logf(f, "Connected. Stub loaded.");

    if(fast_baud) {
        err = esp_loader_change_transmission_rate(fast_baud);
        if(err == ESP_LOADER_SUCCESS) {
            esp_flasher_logf(f, "Speed -> %lu baud", (unsigned long)fast_baud);
        } else {
            esp_flasher_logf(f, "Fast baud failed (%d).", (int)err);
            esp_flasher_logf(f, "Use Safe in Settings.");
            return false; // link may be desynced; bail rather than risk a bad flash
        }
    }
    return true;
}

bool esp_flasher_flash_file(EspFlasher* f, Storage* storage, const char* path, uint32_t addr) {
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        esp_flasher_logf(f, "Cannot open bin.");
        storage_file_free(file);
        return false;
    }
    uint32_t size = (uint32_t)storage_file_size(file);
    esp_flasher_logf(f, "Erasing + writing %luKB...", (unsigned long)(size / 1024));

    esp_loader_error_t err = esp_loader_flash_start(addr, size, FLASH_BLOCK);
    if(err != ESP_LOADER_SUCCESS) {
        esp_flasher_logf(f, "flash_start failed (%d).", (int)err);
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    uint8_t* buf = malloc(FLASH_BLOCK);
    uint32_t done = 0;
    int last_pct = -1;
    bool ok = true;
    while(done < size) {
        if(s_abort) {
            esp_flasher_logf(f, "Aborted.");
            ok = false;
            break;
        }
        uint16_t n = storage_file_read(file, buf, FLASH_BLOCK);
        if(n == 0) break;
        err = esp_loader_flash_write(buf, n);
        if(err != ESP_LOADER_SUCCESS) {
            esp_flasher_logf(f, "write failed @%lu (%d).", (unsigned long)done, (int)err);
            ok = false;
            break;
        }
        done += n;
        int pct = (int)((uint64_t)done * 100 / size);
        if(pct != last_pct && pct % 10 == 0) {
            esp_flasher_logf(f, "  %d%%", pct);
            last_pct = pct;
        }
    }
    free(buf);
    storage_file_close(file);
    storage_file_free(file);

    if(ok) {
        esp_loader_flash_finish(false);
        esp_flasher_logf(f, "Flash complete.");
    }
    return ok;
}

bool esp_flasher_backup(EspFlasher* f, Storage* storage, const char* out_path) {
    uint32_t size = 0;
    esp_loader_error_t err = esp_loader_flash_detect_size(&size);
    if(err != ESP_LOADER_SUCCESS || size == 0) {
        esp_flasher_logf(f, "Flash size unknown (%d).", (int)err);
        return false;
    }
    esp_flasher_logf(f, "Backing up %luKB...", (unsigned long)(size / 1024));

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, out_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        esp_flasher_logf(f, "Cannot create backup file.");
        storage_file_free(file);
        return false;
    }

    uint8_t* buf = malloc(BACKUP_CHUNK);
    uint32_t addr = 0;
    int last_pct = -1;
    bool ok = true;
    while(addr < size) {
        if(s_abort) {
            esp_flasher_logf(f, "Aborted.");
            ok = false;
            break;
        }
        uint32_t n = (size - addr < BACKUP_CHUNK) ? (size - addr) : BACKUP_CHUNK;
        err = esp_loader_flash_read(buf, addr, n);
        if(err != ESP_LOADER_SUCCESS) {
            esp_flasher_logf(f, "read failed @%lu (%d).", (unsigned long)addr, (int)err);
            ok = false;
            break;
        }
        if(storage_file_write(file, buf, n) != n) {
            esp_flasher_logf(f, "SD write failed.");
            ok = false;
            break;
        }
        addr += n;
        int pct = (int)((uint64_t)addr * 100 / size);
        if(pct != last_pct && pct % 10 == 0) {
            esp_flasher_logf(f, "  %d%%", pct);
            last_pct = pct;
        }
    }
    free(buf);
    storage_file_close(file);
    storage_file_free(file);

    if(ok) esp_flasher_logf(f, "Backup saved.");
    return ok;
}
