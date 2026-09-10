// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
//
// helpers/open_drone_id.c -- the ASTM F3411 Remote ID decoder.
//
// This module parses RAW RADIO INPUT that anyone can transmit, on a device with
// ~256 KB of RAM shared with the firmware, and its output is plotted on a map
// and written into shareable reports. So the negatives here matter more than the
// positives: a length field trusted from the air is a buffer overrun, and a
// coordinate accepted from the standard's own "unknown" marker is a map pin in
// the Gulf of Guinea that someone will believe.
#include "test.h"
#include "open_drone_id.h"

#include <math.h>
#include <string.h>

void suite_open_drone_id(void);

// Build one 25-byte message with `type` in the high nibble of byte 0 and
// protocol version 2 in the low nibble, exactly as an aircraft emits it.
static void mk_msg(uint8_t* m, uint8_t type) {
    memset(m, 0, 25);
    m[0] = (uint8_t)((type << 4) | 0x02);
}

// Little-endian writers, mirroring the readers in the module under test. Written
// out longhand rather than memcpy'd from a host int so the test does not inherit
// the host's endianness -- which would make it pass on x86 and lie elsewhere.
static void wr_i32(uint8_t* p, int32_t v) {
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u & 0xff);
    p[1] = (uint8_t)((u >> 8) & 0xff);
    p[2] = (uint8_t)((u >> 16) & 0xff);
    p[3] = (uint8_t)((u >> 24) & 0xff);
}

static void wr_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

void suite_open_drone_id(void) {
    printf("[open_drone_id]\n");

    // --- init: unknown is NAN, never zero -----------------------------------
    OdidReport r;
    odid_report_init(&r);
    CHECK(!r.have_id);
    CHECK(!r.have_location);
    CHECK(!r.have_system);
    CHECK(isnan(r.lat) && isnan(r.lon));
    CHECK(isnan(r.op_lat) && isnan(r.op_lon));
    CHECK(isnan(r.alt_geo_m) && isnan(r.height_m));

    // --- Basic ID: serial + aircraft type -----------------------------------
    uint8_t basic[25];
    mk_msg(basic, OdidMsgBasicId);
    basic[1] = (uint8_t)((OdidIdSerial << 4) | OdidUaMultirotor);
    memcpy(&basic[2], "1596F3AB2C4D5E6F", 16);
    odid_report_init(&r);
    CHECK(odid_parse_messages(basic, sizeof(basic), &r));
    CHECK(r.have_id);
    CHECK_STR_EQ(r.uas_id, "1596F3AB2C4D5E6F");
    CHECK_INT_EQ(r.id_type, OdidIdSerial);
    CHECK_INT_EQ(r.ua_type, OdidUaMultirotor);
    CHECK_STR_EQ(odid_ua_type_str(r.ua_type), "Multirotor");
    CHECK_STR_EQ(odid_id_type_str(r.id_type), "Serial");

    // The id field is a FIXED 20 bytes and an aircraft may fill every one of
    // them, so the decoder must terminate on width rather than on a NUL that is
    // not there. Reading past it would spill the next struct member into a
    // string that ends up on screen and in reports.
    uint8_t full[25];
    mk_msg(full, OdidMsgBasicId);
    full[1] = (uint8_t)((OdidIdCaaRegistration << 4) | OdidUaAeroplane);
    memcpy(&full[2], "ABCDEFGHIJKLMNOPQRST", 20);
    odid_report_init(&r);
    CHECK(odid_parse_messages(full, sizeof(full), &r));
    CHECK_STR_EQ(r.uas_id, "ABCDEFGHIJKLMNOPQRST");
    CHECK_INT_EQ((int)strlen(r.uas_id), 20);
    CHECK_STR_EQ(odid_id_type_str(r.id_type), "CAA reg");

    // Control bytes and high bytes are DROPPED, not substituted. This string is
    // drawn on the display and written into CSV and Markdown; a raw 0x1b or a
    // comma-adjacent control byte from a hostile advert has no business there.
    uint8_t nasty[25];
    mk_msg(nasty, OdidMsgBasicId);
    nasty[1] = (uint8_t)((OdidIdSerial << 4) | OdidUaMultirotor);
    // Built byte by byte ON PURPOSE. Writing this as "AB\x1b[31m\xff\x01CD"
    // does not mean what it looks like: C hex escapes are greedy and 'C'/'D' are
    // hex digits, so \x01CD is ONE out-of-range byte and the letters vanish from
    // the literal. The first version of this test asserted the wrong string for
    // exactly that reason -- a fixture that encodes a misreading of its own input
    // is worse than no fixture.
    {
        const uint8_t raw[] = {'A', 'B', 0x1b, '[', '3', '1', 'm', 0xff, 0x01, 'C', 'D'};
        memcpy(&nasty[2], raw, sizeof(raw));
    }
    odid_report_init(&r);
    CHECK(odid_parse_messages(nasty, sizeof(nasty), &r));
    CHECK_STR_EQ(r.uas_id, "AB[31mCD");

    // Trailing space padding is trimmed, so a padded field matches an unpadded
    // one instead of showing up as a second aircraft.
    uint8_t padded[25];
    mk_msg(padded, OdidMsgBasicId);
    padded[1] = (uint8_t)((OdidIdSerial << 4) | OdidUaMultirotor);
    memcpy(&padded[2], "SKY123    ", 10);
    odid_report_init(&r);
    CHECK(odid_parse_messages(padded, sizeof(padded), &r));
    CHECK_STR_EQ(r.uas_id, "SKY123");

    // --- Location: aircraft position ----------------------------------------
    uint8_t loc[25];
    mk_msg(loc, OdidMsgLocation);
    loc[1] = 0x00; // SpeedMult 0, EW 0
    loc[2] = 90; // direction, east half
    loc[3] = 40; // 40 * 0.25 = 10 m/s
    wr_i32(&loc[4], 407128000); // 40.7128
    wr_i32(&loc[8], -740060000); // -74.0060
    wr_u16(&loc[14], 2400); // geo alt: 2400*0.5 - 1000 = 200 m
    wr_u16(&loc[16], 2200); // height:  2200*0.5 - 1000 = 100 m
    odid_report_init(&r);
    CHECK(odid_parse_messages(loc, sizeof(loc), &r));
    CHECK(r.have_location);
    CHECK(fabsf(r.lat - 40.7128f) < 0.0005f);
    CHECK(fabsf(r.lon - (-74.0060f)) < 0.0005f);
    CHECK(fabsf(r.alt_geo_m - 200.0f) < 0.6f);
    CHECK(fabsf(r.height_m - 100.0f) < 0.6f);
    CHECK(fabsf(r.speed_mps - 10.0f) < 0.01f);
    CHECK(fabsf(r.direction_deg - 90.0f) < 0.01f);

    // The east/west bit is what makes the 0-179 field cover a full circle. Miss
    // it and every westbound aircraft is reported flying the opposite way.
    loc[1] = 0x02; // EWDirection set
    loc[2] = 30; // -> 210 degrees
    odid_report_init(&r);
    CHECK(odid_parse_messages(loc, sizeof(loc), &r));
    CHECK(fabsf(r.direction_deg - 210.0f) < 0.01f);

    // High speed range: enc*0.75 + 255*0.25.
    loc[1] = 0x01; // SpeedMult set
    loc[3] = 100;
    odid_report_init(&r);
    CHECK(odid_parse_messages(loc, sizeof(loc), &r));
    CHECK(fabsf(r.speed_mps - (100.0f * 0.75f + 63.75f)) < 0.01f);

    // Direction 361 is the standard's "unknown" and must not decode as a bearing.
    loc[1] = 0x00;
    loc[2] = 255;
    odid_report_init(&r);
    CHECK(odid_parse_messages(loc, sizeof(loc), &r));
    CHECK(isnan(r.direction_deg));

    // Altitude 0 encodes "unknown" and would otherwise decode to -1000 m, i.e. a
    // drone rendered a kilometre underground.
    uint8_t noalt[25];
    mk_msg(noalt, OdidMsgLocation);
    wr_i32(&noalt[4], 407128000);
    wr_i32(&noalt[8], -740060000);
    wr_u16(&noalt[14], 0);
    wr_u16(&noalt[16], 0);
    odid_report_init(&r);
    CHECK(odid_parse_messages(noalt, sizeof(noalt), &r));
    CHECK(isnan(r.alt_geo_m));
    CHECK(isnan(r.height_m));

    // --- 0/0 IS NOT A LOCATION ----------------------------------------------
    // The standard's own "no value" marker, and simultaneously a real point in
    // the Gulf of Guinea. An aircraft without a fix sends it continuously, so
    // accepting it drops a map pin in the ocean for every drone seen indoors.
    uint8_t zero[25];
    mk_msg(zero, OdidMsgLocation);
    wr_i32(&zero[4], 0);
    wr_i32(&zero[8], 0);
    odid_report_init(&r);
    CHECK(odid_parse_messages(zero, sizeof(zero), &r));
    CHECK(r.have_location); // the message WAS understood ...
    CHECK(isnan(r.lat) && isnan(r.lon)); // ... it just carried no position

    // Off-globe values are rejected too -- a corrupt or hostile advert should not
    // be able to place an aircraft at latitude 300.
    uint8_t insane[25];
    mk_msg(insane, OdidMsgLocation);
    wr_i32(&insane[4], 2000000000); // 200 degrees
    wr_i32(&insane[8], 100000000);
    odid_report_init(&r);
    CHECK(odid_parse_messages(insane, sizeof(insane), &r));
    CHECK(isnan(r.lat) && isnan(r.lon));

    // --- System: THE OPERATOR'S POSITION ------------------------------------
    // The single most useful field in the whole standard for this app, and the
    // reason it is worth decoding at all.
    uint8_t sys[25];
    mk_msg(sys, OdidMsgSystem);
    sys[1] = 0x01; // operator location type: live GPS
    wr_i32(&sys[2], 407200000); // 40.72
    wr_i32(&sys[6], -740100000); // -74.01
    odid_report_init(&r);
    CHECK(odid_parse_messages(sys, sizeof(sys), &r));
    CHECK(r.have_system);
    CHECK(fabsf(r.op_lat - 40.72f) < 0.0005f);
    CHECK(fabsf(r.op_lon - (-74.01f)) < 0.0005f);

    // A System message with no operator fix must not claim to have one, or the
    // UI would offer a bearing to 0,0.
    uint8_t sys0[25];
    mk_msg(sys0, OdidMsgSystem);
    odid_report_init(&r);
    CHECK(odid_parse_messages(sys0, sizeof(sys0), &r));
    CHECK(!r.have_system);
    CHECK(isnan(r.op_lat) && isnan(r.op_lon));

    // --- Operator ID --------------------------------------------------------
    uint8_t opid[25];
    mk_msg(opid, OdidMsgOperatorId);
    memcpy(&opid[2], "FA3ABC123XYZ", 12);
    odid_report_init(&r);
    CHECK(odid_parse_messages(opid, sizeof(opid), &r));
    CHECK(r.have_operator_id);
    CHECK_STR_EQ(r.operator_id, "FA3ABC123XYZ");

    // --- ACCUMULATION ACROSS ADVERTS ----------------------------------------
    // This is how it actually arrives. One BLE legacy advert carries ONE message
    // and the aircraft cycles types, so the serial and the operator position are
    // seconds apart. If each payload reset the report, the app would never hold
    // both at once and the operator location would flicker in and out.
    odid_report_init(&r);
    CHECK(odid_parse_messages(basic, sizeof(basic), &r));
    CHECK(odid_parse_messages(loc, sizeof(loc), &r));
    CHECK(odid_parse_messages(sys, sizeof(sys), &r));
    CHECK(odid_parse_messages(opid, sizeof(opid), &r));
    CHECK(r.have_id && r.have_location && r.have_system && r.have_operator_id);
    CHECK_STR_EQ(r.uas_id, "1596F3AB2C4D5E6F");
    CHECK(fabsf(r.op_lat - 40.72f) < 0.0005f);
    CHECK_STR_EQ(r.operator_id, "FA3ABC123XYZ");

    // A later Basic ID with an EMPTY id must not wipe a serial already held.
    // Aircraft legitimately send several Basic IDs with different id types and
    // only one populated; clobbering would blank the id every other advert.
    uint8_t empty_id[25];
    mk_msg(empty_id, OdidMsgBasicId);
    empty_id[1] = (uint8_t)((OdidIdNone << 4) | OdidUaMultirotor);
    CHECK(odid_parse_messages(empty_id, sizeof(empty_id), &r));
    CHECK_STR_EQ(r.uas_id, "1596F3AB2C4D5E6F");

    // --- BLE transport framing ----------------------------------------------
    uint8_t sd[2 + 25];
    sd[0] = ODID_BLE_APP_CODE;
    sd[1] = 7; // message counter, deliberately not used for dedup
    memcpy(&sd[2], basic, 25);
    odid_report_init(&r);
    CHECK(odid_parse_ble_service_data(sd, sizeof(sd), &r));
    CHECK_STR_EQ(r.uas_id, "1596F3AB2C4D5E6F");

    // Wrong application code -> not Remote ID. Service data under 0xFFFA from
    // something else must not be decoded as an aircraft.
    sd[0] = 0x0C;
    odid_report_init(&r);
    CHECK(!odid_parse_ble_service_data(sd, sizeof(sd), &r));
    CHECK(!r.have_id);

    // Truncated payloads are rejected rather than read past the end.
    sd[0] = ODID_BLE_APP_CODE;
    CHECK(!odid_parse_ble_service_data(sd, 2, &r));
    CHECK(!odid_parse_ble_service_data(sd, 20, &r));
    CHECK(!odid_parse_ble_service_data(sd, 26, &r)); // one byte short of a message
    CHECK(!odid_parse_ble_service_data(NULL, sizeof(sd), &r));

    // --- MESSAGE PACK, including the malicious shapes -----------------------
    uint8_t pack[3 + 25 * 3];
    pack[0] = (uint8_t)((OdidMsgPacked << 4) | 0x02);
    pack[1] = 25; // single message size
    pack[2] = 3; // message count
    memcpy(&pack[3 + 0], basic, 25);
    memcpy(&pack[3 + 25], loc, 25);
    memcpy(&pack[3 + 50], sys, 25);
    odid_report_init(&r);
    CHECK(odid_parse_messages(pack, sizeof(pack), &r));
    CHECK(r.have_id && r.have_location && r.have_system);
    CHECK_STR_EQ(r.uas_id, "1596F3AB2C4D5E6F");
    CHECK(fabsf(r.op_lat - 40.72f) < 0.0005f);

    // A COUNT LARGER THAN THE BUFFER is the obvious way to walk this loop off
    // the end. The count comes from the air; nothing about it is trustworthy.
    // If this check is ever removed, this reads 6 messages out of a 3-message
    // buffer.
    pack[2] = 9;
    odid_report_init(&r);
    CHECK(!odid_parse_messages(pack, sizeof(pack), &r));

    // A count of zero, and a count past the enum's own ceiling.
    pack[2] = 0;
    CHECK(!odid_parse_messages(pack, sizeof(pack), &r));
    pack[2] = 255;
    CHECK(!odid_parse_messages(pack, sizeof(pack), &r));

    // A declared message size other than 25 is malformed by definition.
    pack[2] = 3;
    pack[1] = 24;
    CHECK(!odid_parse_messages(pack, sizeof(pack), &r));
    pack[1] = 0;
    CHECK(!odid_parse_messages(pack, sizeof(pack), &r));

    // --- unknown / unused message types -------------------------------------
    // Auth and Self ID are legal. An aircraft sending one is not malformed, so
    // the payload must not be rejected -- but nothing should be invented from it.
    uint8_t auth[25];
    mk_msg(auth, OdidMsgAuth);
    odid_report_init(&r);
    CHECK(!odid_parse_messages(auth, sizeof(auth), &r)); // nothing we could use
    CHECK(!r.have_id && !r.have_location && !r.have_system);

    // ... but a pack that mixes one in still yields the messages we do use.
    uint8_t mixed[3 + 25 * 2];
    mixed[0] = (uint8_t)((OdidMsgPacked << 4) | 0x02);
    mixed[1] = 25;
    mixed[2] = 2;
    memcpy(&mixed[3], auth, 25);
    memcpy(&mixed[3 + 25], sys, 25);
    odid_report_init(&r);
    CHECK(odid_parse_messages(mixed, sizeof(mixed), &r));
    CHECK(r.have_system);

    // --- labels never claim a shape we were not told ------------------------
    CHECK_STR_EQ(odid_ua_type_str(OdidUaNone), "Aircraft");
    CHECK_STR_EQ(odid_ua_type_str(OdidUaOther), "Aircraft");
    CHECK_STR_EQ(odid_ua_type_str(200), "Aircraft"); // out of range
    CHECK_STR_EQ(odid_ua_type_str(OdidUaAeroplane), "Fixed wing");
    CHECK_STR_EQ(odid_id_type_str(OdidIdNone), "-");
    CHECK_STR_EQ(odid_id_type_str(99), "-");

    // Every label has to fit the detail screen's value column (~12 chars).
    for(uint8_t t = 0; t < 20; t++) {
        CHECK((int)strlen(odid_ua_type_str(t)) <= 12);
    }

    // --- THE BENCH EMITTER'S EXACT BYTES ------------------------------------
    //
    // Copied verbatim from ODID_BASIC_ID / ODID_LOCATION / ODID_SYSTEM in
    // tools/flock_emitter/flock_emitter.ino, which hand-encodes them because an
    // Arduino sketch has no decoder to check itself against.
    //
    // WHY THIS BLOCK EXISTS. The first version of those arrays decoded to
    // -74.0264 and 40.7564 instead of -74.0060 and 40.7200 -- two byte-order
    // mistakes in hand-written little-endian int32s. Both produce perfectly
    // plausible coordinates a few kilometres away, so every hardware test would
    // have passed: a drone appears, it has a serial, it has an operator location,
    // everything renders. Nothing on a 128x64 screen tells you the longitude is
    // two kilometres out. Only running the real decoder over the real bytes does.
    //
    // If the emitter's coordinates change, change them here in the same commit.
    static const uint8_t bench_basic[25] = {0x02, 0x12, 'B', 'E', 'N', 'C', 'H', '-', 'D',
                                            'R',  'O',  'N', 'E', '-', '0', '1', 0,   0,
                                            0,    0,    0,   0,   0,   0,   0};
    static const uint8_t bench_loc[25] = {0x12, 0x00, 90,   40,   0xC0, 0x47, 0x44, 0x18, 0xA0,
                                          0x94, 0xE3, 0xD3, 0x00, 0x00, 0x60, 0x09, 0x98, 0x08,
                                          0,    0,    0,    0,    0,    0,    0};
    static const uint8_t bench_sys[25] = {0x42, 0x01, 0x00, 0x61, 0x45, 0x18, 0x60, 0xF8, 0xE2,
                                          0xD3, 0,    0,    0,    0,    0,    0,    0,    0,
                                          0,    0,    0,    0,    0,    0,    0};

    odid_report_init(&r);
    CHECK(odid_parse_messages(bench_basic, sizeof(bench_basic), &r));
    CHECK(odid_parse_messages(bench_loc, sizeof(bench_loc), &r));
    CHECK(odid_parse_messages(bench_sys, sizeof(bench_sys), &r));
    CHECK_STR_EQ(r.uas_id, "BENCH-DRONE-01");
    CHECK_INT_EQ(r.ua_type, OdidUaMultirotor);
    CHECK(fabsf(r.lat - 40.7128f) < 0.0005f);
    CHECK(fabsf(r.lon - (-74.0060f)) < 0.0005f);
    CHECK(fabsf(r.op_lat - 40.7200f) < 0.0005f);
    CHECK(fabsf(r.op_lon - (-74.0100f)) < 0.0005f);
    CHECK(fabsf(r.alt_geo_m - 200.0f) < 0.6f);
    CHECK(fabsf(r.height_m - 100.0f) < 0.6f);
    // The two positions must stay far enough apart that a test cannot pass by
    // confusing the aircraft with its operator -- roughly a kilometre.
    CHECK(fabsf(r.lat - r.op_lat) > 0.005f);

    // --- defensive: nothing crashes on a NULL report ------------------------
    odid_report_init(NULL);
    CHECK(!odid_parse_messages(basic, sizeof(basic), NULL));
    CHECK(!odid_parse_messages(NULL, 25, &r));
    CHECK(!odid_parse_messages(basic, 0, &r));
}
