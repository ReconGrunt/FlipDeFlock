// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#include "open_drone_id.h"

#include <math.h>
#include <string.h>

// Scaling constants, taken from the reference implementation
// (opendroneid/opendroneid-core-c, libopendroneid/opendroneid.c) rather than
// from prose about the standard -- the encoded forms are not guessable and a
// wrong divisor produces coordinates that look plausible and are wrong, which is
// the worst failure this file could have.
#define ODID_LATLON_MULT 10000000.0f // int32 -> degrees
#define ODID_ALT_DIV     0.5f // uint16 -> metres, then subtract the adder
#define ODID_ALT_ADDER   1000.0f
#define ODID_SPEED_LO    0.25f // SpeedMult == 0
#define ODID_SPEED_HI    0.75f // SpeedMult == 1, plus the low range's full span

// Little-endian readers. The standard is explicit that the wire is
// little-endian, and doing it byte-wise rather than by casting a pointer keeps
// this correct on a strict-alignment target and under -fsanitize=alignment.
static uint16_t rd_u16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t rd_i32(const uint8_t* p) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                 ((uint32_t)p[3] << 24);
    return (int32_t)v;
}

/**
 * Decode an encoded latitude/longitude pair, rejecting the standard's own
 * "unknown" marker and anything off the globe.
 *
 * 0/0 IS NOT A LOCATION. The standard uses it to mean "no value", and it is also
 * a real point in the Gulf of Guinea. A transmitter that has not got a fix yet
 * sends it constantly, so treating it as a sighting would drop a marker in the
 * ocean on every drone seen indoors -- and a plotted point is believed far more
 * readily than a blank field.
 */
static bool decode_latlon(int32_t lat_enc, int32_t lon_enc, float* lat, float* lon) {
    if(lat_enc == 0 && lon_enc == 0) return false;
    float la = (float)lat_enc / ODID_LATLON_MULT;
    float lo = (float)lon_enc / ODID_LATLON_MULT;
    if(la < -90.0f || la > 90.0f || lo < -180.0f || lo > 180.0f) return false;
    *lat = la;
    *lon = lo;
    return true;
}

/**
 * Copy a fixed-width ASTM text field out as a NUL-terminated C string.
 *
 * The field is NOT NUL-terminated on the wire -- it is a fixed 20 bytes, padded
 * with NULs or spaces, and an aircraft may legally fill all 20. Everything
 * outside printable ASCII is dropped rather than substituted: this string is
 * rendered on a 128x64 screen and written into CSV and Markdown reports, and a
 * stray control byte from a malformed or hostile advert has no business reaching
 * any of those. Trailing whitespace is trimmed so a padded field compares equal
 * to an unpadded one.
 */
static void copy_text(const uint8_t* src, size_t n, char* dst, size_t dst_len) {
    size_t o = 0;
    for(size_t i = 0; i < n && o + 1 < dst_len; i++) {
        char c = (char)src[i];
        if(c == '\0') break;
        if(c >= 0x20 && c <= 0x7e) dst[o++] = c;
    }
    while(o > 0 && dst[o - 1] == ' ') o--;
    dst[o] = '\0';
}

void odid_report_init(OdidReport* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    out->lat = NAN;
    out->lon = NAN;
    out->alt_geo_m = NAN;
    out->height_m = NAN;
    out->speed_mps = NAN;
    out->direction_deg = NAN;
    out->op_lat = NAN;
    out->op_lon = NAN;
}

/** Decode exactly one 25-byte message into `out`. Returns true if we used it. */
static bool parse_one(const uint8_t* m, OdidReport* out) {
    uint8_t type = (uint8_t)(m[0] >> 4);

    switch(type) {
    case OdidMsgBasicId: {
        // Byte 1 packs both nibbles: id type high, aircraft type low.
        uint8_t id_type = (uint8_t)(m[1] >> 4);
        uint8_t ua_type = (uint8_t)(m[1] & 0x0f);
        char id[21];
        copy_text(&m[2], 20, id, sizeof(id));
        // A Basic ID with an EMPTY id still carries the aircraft type, which is
        // worth keeping -- but it must not overwrite a serial we already have.
        // Some aircraft send several Basic ID messages with different id types
        // (a serial and a CAA registration), and only one of them is populated.
        if(id[0]) {
            memcpy(out->uas_id, id, sizeof(id));
            out->id_type = id_type;
        }
        out->ua_type = ua_type;
        out->have_id = true;
        return true;
    }

    case OdidMsgLocation: {
        uint8_t flags = m[1];
        uint8_t speed_mult = (uint8_t)(flags & 0x01);
        uint8_t ew_direction = (uint8_t)((flags >> 1) & 0x01);

        float lat, lon;
        if(decode_latlon(rd_i32(&m[4]), rd_i32(&m[8]), &lat, &lon)) {
            out->lat = lat;
            out->lon = lon;
        }

        // Direction is 0-179 on the wire with a separate east/west bit, so the
        // full 0-359 range is reconstructed rather than read. 361 is the
        // standard's "unknown".
        float dir = (float)m[2];
        if(ew_direction) dir += 180.0f;
        out->direction_deg = (m[2] <= 179) ? dir : NAN;

        out->speed_mps = speed_mult ?
                             ((float)m[3] * ODID_SPEED_HI + (255.0f * ODID_SPEED_LO)) :
                             ((float)m[3] * ODID_SPEED_LO);

        uint16_t alt_geo = rd_u16(&m[14]);
        uint16_t height = rd_u16(&m[16]);
        // 0 encodes "unknown" for both, and decodes to -1000 m -- which would
        // otherwise render as a drone a kilometre underground.
        out->alt_geo_m = alt_geo ? ((float)alt_geo * ODID_ALT_DIV - ODID_ALT_ADDER) : NAN;
        out->height_m = height ? ((float)height * ODID_ALT_DIV - ODID_ALT_ADDER) : NAN;

        out->have_location = true;
        return true;
    }

    case OdidMsgSystem: {
        // THE OPERATOR'S POSITION. Byte 1's low two bits say where it came from
        // (0 = takeoff point, 1 = live GPS, 2 = fixed). All three are the human,
        // not the aircraft, so all three are kept; the distinction is not one we
        // could act on differently.
        float lat, lon;
        if(decode_latlon(rd_i32(&m[2]), rd_i32(&m[6]), &lat, &lon)) {
            out->op_lat = lat;
            out->op_lon = lon;
            out->have_system = true;
        }
        return true;
    }

    case OdidMsgOperatorId: {
        // Byte 1 is the operator-id type; byte 2 onward is the id itself.
        char id[21];
        copy_text(&m[2], 20, id, sizeof(id));
        if(id[0]) {
            memcpy(out->operator_id, id, sizeof(id));
            out->have_operator_id = true;
        }
        return true;
    }

    default:
        // Auth (0x2) and Self ID (0x3) are legal and we do not use them. Not an
        // error -- an aircraft sending one is saying something, just not
        // something that changes what we show.
        return false;
    }
}

bool odid_parse_messages(const uint8_t* msgs, size_t len, OdidReport* out) {
    if(!msgs || !out || len < ODID_MESSAGE_SIZE) return false;

    // A message PACK is one message-shaped header carrying several messages, and
    // is what BLE 5 extended advertising and the Wi-Fi transports use. Handled
    // here rather than by the caller so both transports get it for free.
    if((msgs[0] >> 4) == OdidMsgPacked) {
        uint8_t msg_size = msgs[1];
        uint8_t count = msgs[2];
        // Trust nothing about the counts: this is attacker-reachable radio input.
        // A pack claiming more messages than the payload can hold is the obvious
        // way to walk this loop off the end of the buffer.
        if(msg_size != ODID_MESSAGE_SIZE) return false;
        if(count == 0 || count > 9) return false;
        if((size_t)count * ODID_MESSAGE_SIZE + 3u > len) return false;
        bool any = false;
        for(uint8_t i = 0; i < count; i++) {
            any |= parse_one(&msgs[3 + (size_t)i * ODID_MESSAGE_SIZE], out);
        }
        return any;
    }

    bool any = false;
    for(size_t off = 0; off + ODID_MESSAGE_SIZE <= len; off += ODID_MESSAGE_SIZE) {
        any |= parse_one(&msgs[off], out);
    }
    return any;
}

bool odid_parse_ble_service_data(const uint8_t* sd, size_t len, OdidReport* out) {
    if(!sd || !out) return false;
    // app code + message counter + at least one message
    if(len < 2u + ODID_MESSAGE_SIZE) return false;
    if(sd[0] != ODID_BLE_APP_CODE) return false;
    // sd[1] is a per-message sequence counter used to spot dropped adverts. We
    // do not deduplicate on it: an aircraft cycles message TYPES, so consecutive
    // adverts differ in content as well as counter, and every one of them is
    // wanted.
    return odid_parse_messages(&sd[2], len - 2u, out);
}

const char* odid_ua_type_str(uint8_t ua_type) {
    switch(ua_type) {
    case OdidUaAeroplane:
        return "Fixed wing";
    case OdidUaMultirotor:
        return "Multirotor";
    case OdidUaGyroplane:
        return "Gyroplane";
    case OdidUaHybridLift:
        return "Hybrid lift";
    case OdidUaOrnithopter:
        return "Ornithopter";
    case OdidUaGlider:
        return "Glider";
    case OdidUaKite:
        return "Kite";
    case OdidUaFreeBalloon:
        return "Balloon";
    case OdidUaCaptiveBalloon:
        return "Moored bln";
    case OdidUaAirship:
        return "Airship";
    case OdidUaParachute:
        return "Parachute";
    case OdidUaRocket:
        return "Rocket";
    case OdidUaTethered:
        return "Tethered";
    case OdidUaGroundObstacle:
        return "Ground obs";
    default:
        // Covers both OdidUaNone and OdidUaOther, and anything the standard adds
        // later. "Aircraft" is the honest floor: Remote ID was received, so
        // something is flying, and claiming a shape we were not told is exactly
        // the over-claim the class enum exists to prevent.
        return "Aircraft";
    }
}

const char* odid_id_type_str(uint8_t id_type) {
    switch(id_type) {
    case OdidIdSerial:
        return "Serial";
    case OdidIdCaaRegistration:
        return "CAA reg";
    case OdidIdUtmUuid:
        return "UTM UUID";
    case OdidIdSessionId:
        return "Session";
    default:
        return "-";
    }
}
