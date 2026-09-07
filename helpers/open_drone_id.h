// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
/**
 * @file open_drone_id.h
 * Decoder for ASTM F3411 / ASD-STAN Remote ID broadcasts ("Open Drone ID").
 *
 * WHY THIS EXISTS, AND WHY IT IS NOT AN OUI TABLE.
 *
 * Police drone programmes are the fastest-growing surveillance deployment there
 * is, and the vendors doing it are almost invisible to prefix matching. Checked
 * against the IEEE registry on 2026-09-07: of the five vendors a US department
 * realistically buys from today -- Skydio, BRINC, Aerodome, Flock and Paladin --
 * only Skydio holds an IEEE block at all (38:1d:14). BRINC, Aerodome and Paladin
 * hold NOTHING, so their radios transmit under whatever module vendor they
 * bought, which is the shared-prefix trap that already forced Flock's Liteon
 * prefixes down to "possible". An OUI table cannot see the modern fleet.
 *
 * Remote ID can. It is a LEGAL MANDATE (FAA 14 CFR Part 89), it is broadcast in
 * the clear with no association or pairing, and it is vendor-independent by
 * construction: the aircraft announces its own serial number, its own position,
 * and -- in the System message -- THE OPERATOR'S POSITION. A drone that is
 * complying with federal law is telling us where its pilot is standing.
 *
 * That last field is why this is worth the code. Everything else FlipDeFlock
 * detects is fixed infrastructure you can walk away from. A drone follows you,
 * and the operator location is the one piece of information that changes what a
 * person can actually do about it.
 *
 * PASSIVE, like everything else here: this only ever decodes bytes the aircraft
 * is already legally required to shout. Nothing in this file transmits, and
 * nothing here can interfere with an aircraft.
 *
 * DECODING HAPPENS ON THIS SIDE, NOT ON THE COMPANION. The companion recognises
 * the transport (BLE service data under UUID 0xFFFA with the 0x0D application
 * code) and forwards the raw bytes as hex; all interpretation is here, where it
 * is pure C and host-tested. That is deliberate and it is the same split the OUI
 * tables should have had: one implementation of the tricky part, in the place
 * that has tests, rather than a second copy in an Arduino sketch that nothing
 * can exercise. See the flock_ouis[] comment in flock_db.c for what the other
 * arrangement costs.
 *
 * Pure (libc + math only), so the whole decoder is exercised by test/.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** ASTM message types (high nibble of a message's first byte). */
typedef enum {
    OdidMsgBasicId = 0x0, /**< UAS id + aircraft type */
    OdidMsgLocation = 0x1, /**< aircraft position / vector */
    OdidMsgAuth = 0x2,
    OdidMsgSelfId = 0x3,
    OdidMsgSystem = 0x4, /**< OPERATOR position */
    OdidMsgOperatorId = 0x5,
    OdidMsgPacked = 0xF, /**< several of the above in one payload */
} OdidMsgType;

/** ASTM UA (unmanned aircraft) type, low nibble of Basic ID byte 1. */
typedef enum {
    OdidUaNone = 0,
    OdidUaAeroplane = 1,
    OdidUaMultirotor = 2, /**< the shape essentially every police drone is */
    OdidUaGyroplane = 3,
    OdidUaHybridLift = 4,
    OdidUaOrnithopter = 5,
    OdidUaGlider = 6,
    OdidUaKite = 7,
    OdidUaFreeBalloon = 8,
    OdidUaCaptiveBalloon = 9,
    OdidUaAirship = 10,
    OdidUaParachute = 11,
    OdidUaRocket = 12,
    OdidUaTethered = 13,
    OdidUaGroundObstacle = 14,
    OdidUaOther = 15,
} OdidUaType;

/** ASTM id type, high nibble of Basic ID byte 1. */
typedef enum {
    OdidIdNone = 0,
    OdidIdSerial = 1, /**< manufacturer serial (ANSI/CTA-2063-A) */
    OdidIdCaaRegistration = 2, /**< the registration a department files */
    OdidIdUtmUuid = 3,
    OdidIdSessionId = 4,
} OdidIdType;

/**
 * Everything we have learned about one aircraft so far.
 *
 * ACCUMULATED ACROSS ADVERTS, not filled by a single one. A BLE legacy advert
 * carries ONE 25-byte message, and an aircraft cycles through the message types,
 * so the serial arrives in one advert and the operator position in another,
 * seconds apart. Callers keep one of these per aircraft and feed every payload
 * into it; each `have_*` flag latches as its message type is first seen.
 *
 * Coordinates are NAN when unknown, never 0. The standard uses 0/0 as its own
 * "no value" marker, which is also a real point in the Gulf of Guinea -- writing
 * that into a map as a sighting is the kind of thing that gets believed.
 */
typedef struct {
    bool have_id;
    char uas_id[21]; /**< NUL-terminated; non-printables stripped */
    uint8_t id_type; /**< OdidIdType */
    uint8_t ua_type; /**< OdidUaType */

    bool have_location;
    float lat, lon; /**< AIRCRAFT position, NAN if not yet known */
    float alt_geo_m; /**< geodetic altitude, metres; NAN if unknown */
    float height_m; /**< height above takeoff/ground; NAN if unknown */
    float speed_mps; /**< horizontal speed; NAN if unknown */
    float direction_deg; /**< track, 0-359; NAN if unknown */

    bool have_system;
    float op_lat, op_lon; /**< OPERATOR position, NAN if not yet known */

    bool have_operator_id;
    char operator_id[21]; /**< operator registration, NUL-terminated */
} OdidReport;

/** Reset a report to "nothing known" (all coordinates NAN, all flags false). */
void odid_report_init(OdidReport* out);

/**
 * The 16-bit BLE service UUID Remote ID is carried under, and the application
 * code that must be the first byte of that service data. Both are fixed by the
 * standard; the companion matches on them and forwards what follows.
 */
#define ODID_BLE_SERVICE_UUID 0xFFFA
#define ODID_BLE_APP_CODE     0x0D

/** One encoded message is always exactly this many bytes. */
#define ODID_MESSAGE_SIZE 25

/**
 * Decode one BLE Remote ID service-data payload into `out`, merging with what
 * is already there.
 *
 * `sd` points at the APPLICATION CODE (0x0D), i.e. what follows the 16-bit UUID
 * in the AD structure. Layout is: app code, a 1-byte message counter, then one
 * 25-byte message -- or, for a message pack, a pack header and several.
 *
 * Returns true when at least one message was understood. Unknown message types
 * (Auth, Self ID) are skipped without failing the payload: an aircraft that
 * sends one is not malformed, it is just saying something we do not use.
 */
bool odid_parse_ble_service_data(const uint8_t* sd, size_t len, OdidReport* out);

/**
 * Decode a bare message stream (no app code, no counter) -- one or more 25-byte
 * messages back to back, or a single message pack. This is the Wi-Fi beacon /
 * NAN shape, and the unit the BLE path funnels into after stripping its header.
 */
bool odid_parse_messages(const uint8_t* msgs, size_t len, OdidReport* out);

/** Short label for a UA type, for a list row ("Multirotor", "Fixed wing"). */
const char* odid_ua_type_str(uint8_t ua_type);

/** Short label for an id type ("Serial", "CAA reg"). */
const char* odid_id_type_str(uint8_t id_type);
