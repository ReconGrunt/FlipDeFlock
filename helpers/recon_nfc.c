#include "recon_nfc.h"
#include "../recon_app_i.h"

#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_device.h>
#include <nfc/protocols/nfc_protocol.h>

#include <string.h>

#define RECON_NFC_MAX_PROTO 8

struct ReconNfc {
    ReconApp* app;
    Nfc* nfc;
    NfcScanner* scanner;
    FuriMutex* lock;
    bool running;
    bool detected;
    size_t proto_num;
    NfcProtocol protos[RECON_NFC_MAX_PROTO];
};

/** Depth of a protocol in its parent hierarchy (more parents = more specific). */
static int proto_depth(NfcProtocol p) {
    int depth = 0;
    NfcProtocol cur = p;
    while(true) {
        NfcProtocol parent = nfc_protocol_get_parent(cur);
        if(parent == NfcProtocolInvalid) break;
        depth++;
        cur = parent;
        if(depth > 8) break; // safety
    }
    return depth;
}

static void recon_nfc_grade(NfcProtocol p, const char** grade, const char** detail) {
    switch(p) {
    case NfcProtocolMfClassic:
        *grade = "WEAK";
        *detail = "MIFARE Classic\nCrypto1 is broken.\nKeys recoverable via\nnested/darkside/mfkey.\nAvoid for access control.";
        break;
    case NfcProtocolMfUltralight:
        *grade = "WEAK";
        *detail = "MIFARE Ultralight\nLittle/no auth on most\nvariants; easily cloned.\nUL-C/EV1 better if PWD set.";
        break;
    case NfcProtocolSt25tb:
        *grade = "WEAK";
        *detail = "ST25TB / SRT\nNo crypto; UID-based.\nCloneable.";
        break;
    case NfcProtocolSlix:
        *grade = "WEAK";
        *detail = "ICODE SLIX (15693)\nVicinity tag; weak/optional\nprotection. Often cloneable.";
        break;
    case NfcProtocolMfPlus:
        *grade = "MEDIUM";
        *detail = "MIFARE Plus\nStrong in SL3 (AES),\nweak if left in SL1.\nVerify security level.";
        break;
    case NfcProtocolMfDesfire:
        *grade = "STRONG";
        *detail = "MIFARE DESFire\nAES/3DES if configured.\nStrong when keys are\nnot left at default.";
        break;
    case NfcProtocolIso14443_4a:
    case NfcProtocolIso14443_4b:
        // Covers ISO14443-4 smartcards including EMV / Type4 / NTAG4xx, whose
        // dedicated enum values are firmware-specific (Momentum) and absent in
        // stock OFW. Grading them here keeps the app portable across both.
        *grade = "INFO";
        *detail = "ISO14443-4 smartcard\nEMV/DESFire/Type4 etc.\nSecurity depends on applet.\nInspect further.";
        break;
    case NfcProtocolFelica:
        *grade = "INFO";
        *detail = "FeliCa\nSecurity is service-defined.";
        break;
    case NfcProtocolIso15693_3:
        *grade = "INFO";
        *detail = "ISO15693 vicinity tag\nOften UID-only. Check use.";
        break;
    case NfcProtocolIso14443_3a:
        *grade = "WEAK";
        *detail = "ISO14443-3A (UID only)\nNo app-layer auth detected.\nUID is cloneable; weak if\nused alone for access.";
        break;
    case NfcProtocolIso14443_3b:
        *grade = "INFO";
        *detail = "ISO14443-3B\nBase layer; inspect apps.";
        break;
    default:
        *grade = "INFO";
        *detail = "Unclassified protocol.";
        break;
    }
}

static void recon_nfc_scanner_cb(NfcScannerEvent event, void* context) {
    ReconNfc* n = context;
    if(event.type == NfcScannerEventTypeDetected) {
        furi_mutex_acquire(n->lock, FuriWaitForever);
        n->proto_num = MIN(event.data.protocol_num, (size_t)RECON_NFC_MAX_PROTO);
        for(size_t i = 0; i < n->proto_num; i++) {
            n->protos[i] = event.data.protocols[i];
        }
        n->detected = (n->proto_num > 0);
        furi_mutex_release(n->lock);
    }
}

ReconNfc* recon_nfc_alloc(void* app) {
    ReconNfc* n = malloc(sizeof(ReconNfc));
    memset(n, 0, sizeof(ReconNfc));
    n->app = app;
    n->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    return n;
}

void recon_nfc_free(ReconNfc* n) {
    furi_assert(n);
    if(n->running) recon_nfc_stop(n);
    furi_mutex_free(n->lock);
    free(n);
}

void recon_nfc_start(ReconNfc* n) {
    if(n->running) return;
    n->detected = false;
    n->proto_num = 0;
    n->nfc = nfc_alloc();
    n->scanner = nfc_scanner_alloc(n->nfc);
    nfc_scanner_start(n->scanner, recon_nfc_scanner_cb, n);
    n->running = true;
}

void recon_nfc_stop(ReconNfc* n) {
    if(!n->running) return;
    nfc_scanner_stop(n->scanner);
    nfc_scanner_free(n->scanner);
    nfc_free(n->nfc);
    n->scanner = NULL;
    n->nfc = NULL;
    n->running = false;
}

bool recon_nfc_get(ReconNfc* n, FuriString* title, FuriString* grade, FuriString* detail) {
    furi_mutex_acquire(n->lock, FuriWaitForever);
    bool detected = n->detected;
    NfcProtocol best = NfcProtocolInvalid;
    int best_depth = -1;
    for(size_t i = 0; i < n->proto_num; i++) {
        int d = proto_depth(n->protos[i]);
        if(d > best_depth) {
            best_depth = d;
            best = n->protos[i];
        }
    }
    furi_mutex_release(n->lock);

    if(!detected || best == NfcProtocolInvalid) {
        return false;
    }

    const char* g = "INFO";
    const char* d = "";
    recon_nfc_grade(best, &g, &d);

    if(title) furi_string_set(title, nfc_device_get_protocol_name(best));
    if(grade) furi_string_set(grade, g);
    if(detail) furi_string_set(detail, d);
    return true;
}
