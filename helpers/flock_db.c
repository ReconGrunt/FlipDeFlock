#include "flock_db.h"
#include <string.h>

/**
 * 31 OUI prefixes observed in fielded Flock Safety deployments.
 * First 30 from @NitekryDPaul research; last (82:6b:f2) from DeFlockJoplin
 * field testing. These are generic vendor prefixes (Liteon, Espressif, etc.),
 * hence OUI-only matches are scored "possible", never "confirmed".
 */
static const uint8_t flock_ouis[][3] = {
    {0x70, 0xc9, 0x4e}, {0x3c, 0x91, 0x80}, {0xd8, 0xf3, 0xbc}, {0x80, 0x30, 0x49},
    {0xb8, 0x35, 0x32}, {0x14, 0x5a, 0xfc}, {0x74, 0x4c, 0xa1}, {0x08, 0x3a, 0x88},
    {0x9c, 0x2f, 0x9d}, {0xc0, 0x35, 0x32}, {0x94, 0x08, 0x53}, {0xe4, 0xaa, 0xea},
    {0xf4, 0x6a, 0xdd}, {0xf8, 0xa2, 0xd6}, {0x24, 0xb2, 0xb9}, {0x00, 0xf4, 0x8d},
    {0xd0, 0x39, 0x57}, {0xe8, 0xd0, 0xfc}, {0xe0, 0x4f, 0x43}, {0xb8, 0x1e, 0xa4},
    {0x70, 0x08, 0x94}, {0x58, 0x8e, 0x81}, {0xec, 0x1b, 0xbd}, {0x3c, 0x71, 0xbf},
    {0x58, 0x00, 0xe3}, {0x90, 0x35, 0xea}, {0x5c, 0x93, 0xa2}, {0x64, 0x6e, 0x69},
    {0x48, 0x27, 0xea}, {0xa4, 0xcf, 0x12}, {0x82, 0x6b, 0xf2},
    {0xb4, 0x1e, 0x52}, // Flock Safety's own registered OUI (GainSec)
};

#define FLOCK_OUI_COUNT (sizeof(flock_ouis) / sizeof(flock_ouis[0]))

size_t flock_oui_count(void) {
    return FLOCK_OUI_COUNT;
}

const uint8_t* flock_oui_get(size_t index) {
    if(index >= FLOCK_OUI_COUNT) return NULL;
    return flock_ouis[index];
}

bool flock_oui_match(const uint8_t* mac) {
    if(!mac) return false;
    for(size_t i = 0; i < FLOCK_OUI_COUNT; i++) {
        if(mac[0] == flock_ouis[i][0] && mac[1] == flock_ouis[i][1] &&
           mac[2] == flock_ouis[i][2]) {
            return true;
        }
    }
    return false;
}

/** ASCII lower-case (no locale, safe for embedded). */
static char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/** Case-insensitive substring search (needle assumed already lower-case). */
static bool ci_contains(const char* haystack, const char* needle_lower) {
    if(!haystack || !needle_lower) return false;
    size_t nlen = strlen(needle_lower);
    if(nlen == 0) return false;
    for(const char* h = haystack; *h; h++) {
        size_t k = 0;
        while(needle_lower[k] && ascii_lower(h[k]) == needle_lower[k]) {
            k++;
        }
        if(k == nlen) return true;
    }
    return false;
}

FlockConfidence flock_ssid_confidence(const char* ssid) {
    if(!ssid || ssid[0] == '\0') return FlockConfidenceNone;

    // Strong, near-unique naming patterns -> confirmed.
    // "Flock-XXXXXX" provisioning APs and the "test_flck" service SSID.
    if(ci_contains(ssid, "flock-") || ci_contains(ssid, "test_flck")) {
        return FlockConfidenceConfirmed;
    }

    // Weaker substrings -> likely (could be a coincidental network name).
    if(ci_contains(ssid, "flock") || ci_contains(ssid, "flck")) {
        return FlockConfidenceLikely;
    }

    return FlockConfidenceNone;
}

FlockConfidence flock_score(const uint8_t* mac, const char* ssid, bool is_probe_req) {
    FlockConfidence by_ssid = flock_ssid_confidence(ssid);
    if(by_ssid == FlockConfidenceConfirmed) return FlockConfidenceConfirmed;

    bool oui = flock_oui_match(mac);

    // OUI + the camera's phone-home probe behaviour is a strong combination.
    if(oui && is_probe_req) {
        return FlockConfidenceLikely > by_ssid ? FlockConfidenceLikely : by_ssid;
    }
    if(oui) {
        return FlockConfidencePossible > by_ssid ? FlockConfidencePossible : by_ssid;
    }

    return by_ssid;
}

const char* flock_confidence_str(FlockConfidence confidence) {
    switch(confidence) {
    case FlockConfidenceConfirmed:
        return "CONFIRMED";
    case FlockConfidenceLikely:
        return "Likely";
    case FlockConfidencePossible:
        return "Possible";
    case FlockConfidenceNone:
    default:
        return "-";
    }
}
