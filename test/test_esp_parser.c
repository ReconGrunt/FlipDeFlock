// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
//
// Adversarial line fixtures for the companion wire-protocol parser (R2 split:
// parse != mutate). Verifies each line type decodes to the right tagged record
// with the right fields, and that malformed/short/injection-flavoured lines are
// rejected (EspMsgIgnore) -- the whole reason the parser was made pure.
#include "esp_parser.h"
#include "flock_db.h"
#include "test.h"

#include <string.h>
#include <stdio.h>

// The parser is DESTRUCTIVE (splits fields in place) and record string fields
// point INTO the buffer, so keep the caller's buffer alive across the asserts.
static EspMsgType parse_into(char* buf, size_t bufsz, const char* s, EspMsg* out) {
    size_t n = strlen(s);
    if(n >= bufsz) n = bufsz - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
    return esp_parse_companion_line(buf, out);
}

static bool mac_eq(const uint8_t* m, const uint8_t* want) {
    return memcmp(m, want, 6) == 0;
}

void suite_esp_parser(void) {
    printf("[esp_parser]\n");
    char buf[256];
    EspMsg m;
    const uint8_t A1F6[6] = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
#define P(s) parse_into(buf, sizeof(buf), (s), &m)

    // --- esp_split_fields ---------------------------------------------------
    {
        char s1[] = "a,b,c";
        char* f[4];
        CHECK_INT_EQ(esp_split_fields(s1, f, 4), 3);
        CHECK_STR_EQ(f[0], "a");
        CHECK_STR_EQ(f[1], "b");
        CHECK_STR_EQ(f[2], "c");
        char s2[] = "a,,c"; // empty middle field preserved
        CHECK_INT_EQ(esp_split_fields(s2, f, 4), 3);
        CHECK_STR_EQ(f[1], "");
        char s3[] = "abc"; // no commas -> single field
        CHECK_INT_EQ(esp_split_fields(s3, f, 4), 1);
        char s4[] = "a,b,c,d,e"; // capped at max: remainder stays in the last field
        CHECK_INT_EQ(esp_split_fields(s4, f, 3), 3);
        CHECK_STR_EQ(f[2], "c,d,e");
    }

    // --- D: Flock detection -------------------------------------------------
    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-40,6,P,3,MyFlock"), EspMsgFlock);
    CHECK(mac_eq(m.u.flock.mac, A1F6));
    CHECK_INT_EQ(m.u.flock.rssi, -40);
    CHECK_INT_EQ(m.u.flock.channel, 6);
    CHECK_INT_EQ(m.u.flock.ftype, 'P');
    CHECK_INT_EQ(m.u.flock.conf, FlockConfidenceConfirmed);
    CHECK_STR_EQ(m.u.flock.ssid, "MyFlock");
    CHECK_INT_EQ(m.u.flock.fp, 0);

    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-50,1,B,1"), EspMsgFlock); // minimal (n=6), no ssid
    CHECK_STR_EQ(m.u.flock.ssid, "");
    CHECK_INT_EQ(m.u.flock.conf, FlockConfidencePossible);
    CHECK_INT_EQ(m.u.flock.ftype, 'B');

    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-50,1,,2,x"), EspMsgFlock); // empty type -> 'O'
    CHECK_INT_EQ(m.u.flock.ftype, 'O');
    CHECK_INT_EQ(m.u.flock.conf, FlockConfidenceLikely);

    CHECK_INT_EQ(P("D,zzzz,-40,6,P,3,x"), EspMsgIgnore); // bad hex mac
    CHECK_INT_EQ(P("D,a1b2,-40,6,P,3,x"), EspMsgIgnore); // mac too short (<12)
    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-40,6,P"), EspMsgIgnore); // too few fields (n<6)

    // SSID that literally begins "fp=" must NOT be read as the IE-fingerprint
    // (the scan starts at f[7], after the ssid at f[6]).
    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-40,6,P,3,fp=1234"), EspMsgFlock);
    CHECK_STR_EQ(m.u.flock.ssid, "fp=1234");
    CHECK_INT_EQ(m.u.flock.fp, 0);

    // Real trailing fp=, no table match (built-in table ships empty): fp passes
    // through, confidence/type unchanged.
    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-40,6,P,2,name,fp=deadbeef"), EspMsgFlock);
    CHECK_INT_EQ((long)m.u.flock.fp, (long)0xdeadbeefu);
    CHECK_INT_EQ(m.u.flock.conf, FlockConfidenceLikely);
    CHECK_INT_EQ(m.u.flock.ftype, 'P');

    // A registered UNVERIFIED user fp upgrades to Class? (ProbeFp) + ftype 'F',
    // never Confirmed. This is the fp confidence logic, now unit-testable.
    static const uint32_t ufps[] = {0xdeadbeef};
    FlockDbExtras ex_fp = {.ie_fps = ufps, .ie_fp_count = 1};
    flock_db_set_extras(&ex_fp);
    CHECK_INT_EQ(P("D,a1b2c3d4e5f6,-40,6,P,0,,fp=deadbeef"), EspMsgFlock);
    CHECK_INT_EQ(m.u.flock.conf, FlockConfidenceProbeFp);
    CHECK_INT_EQ(m.u.flock.ftype, 'F');
    flock_db_set_extras(NULL);

    // --- W: WiFi AP ---------------------------------------------------------
    CHECK_INT_EQ(P("W,a1b2c3d4e5f6,-55,11,3,4,4,0,HomeNet"), EspMsgWifiAp);
    CHECK(mac_eq(m.u.wifi.bssid, A1F6));
    CHECK_INT_EQ(m.u.wifi.rssi, -55);
    CHECK_INT_EQ(m.u.wifi.channel, 11);
    CHECK_INT_EQ(m.u.wifi.auth, 3);
    CHECK_INT_EQ(m.u.wifi.pairwise, 4);
    CHECK_INT_EQ(m.u.wifi.wps, 0);
    CHECK_STR_EQ(m.u.wifi.ssid, "HomeNet");

    CHECK_INT_EQ(P("W,a1b2c3d4e5f6,-55,11,3,4,4,1,Net"), EspMsgWifiAp); // wps=1
    CHECK_INT_EQ(m.u.wifi.wps, 1);
    CHECK_INT_EQ(P("W,a1b2c3d4e5f6,-55,11,3,4,4,0"), EspMsgWifiAp); // n=8, no ssid
    CHECK_STR_EQ(m.u.wifi.ssid, "");
    CHECK_INT_EQ(P("W,a1b2c3d4e5f6,-55,11,3,4,4"), EspMsgIgnore); // n=7 < 8
    CHECK_INT_EQ(P("W,xx,-55,11,3,4,4,0,Net"), EspMsgIgnore); // bad mac

    // --- BLE: device -------------------------------------------------------
    CHECK_INT_EQ(P("BLE,a1b2c3d4e5f6,-60,1,2504,Tag,09c8aabb,rv=1"), EspMsgBleDev);
    CHECK(mac_eq(m.u.ble.addr, A1F6));
    CHECK_INT_EQ(m.u.ble.rssi, -60);
    CHECK_INT_EQ(m.u.ble.cat, 1);
    CHECK_INT_EQ(m.u.ble.company, 2504);
    CHECK_STR_EQ(m.u.ble.name, "Tag");
    CHECK_INT_EQ((long)m.u.ble.mfg_len, 4);
    CHECK(m.u.ble.mfg[0] == 0x09 && m.u.ble.mfg[1] == 0xc8 && m.u.ble.mfg[2] == 0xaa &&
          m.u.ble.mfg[3] == 0xbb);
    CHECK_INT_EQ(m.u.ble.raven_gatt, 1);

    // Trailers in the other order (rv=1 then mfghex) still decode correctly.
    CHECK_INT_EQ(P("BLE,a1b2c3d4e5f6,-60,1,2504,Tag,rv=1,09c8"), EspMsgBleDev);
    CHECK_INT_EQ(m.u.ble.raven_gatt, 1);
    CHECK_INT_EQ((long)m.u.ble.mfg_len, 2);
    CHECK_STR_EQ(m.u.ble.name, "Tag");

    CHECK_INT_EQ(P("BLE,a1b2c3d4e5f6,-60,0,0"), EspMsgBleDev); // minimal (n=5), no name
    CHECK_STR_EQ(m.u.ble.name, "");
    CHECK_INT_EQ((long)m.u.ble.mfg_len, 0);
    CHECK_INT_EQ(m.u.ble.raven_gatt, 0);

    CHECK_INT_EQ(P("BLE,xx,-60,1,2504,Tag"), EspMsgIgnore); // bad mac
    CHECK_INT_EQ(P("BLE,a1b2c3d4e5f6,-60,1"), EspMsgIgnore); // n=4 < 5

    // --- S: status heartbeat ------------------------------------------------
    CHECK_INT_EQ(P("S,1000,50,6,3"), EspMsgStatus);
    CHECK_INT_EQ((long)m.u.status.frames, 1000);
    CHECK_INT_EQ((long)m.u.status.hits, 50);
    CHECK_INT_EQ(m.u.status.channel, 6);
    CHECK_INT_EQ(m.u.status.have_deauths, 1);
    CHECK_INT_EQ((long)m.u.status.deauths, 3);
    CHECK_INT_EQ(P("S,1000,50,6"), EspMsgStatus); // n=4, no deauth count
    CHECK_INT_EQ(m.u.status.have_deauths, 0);
    CHECK_INT_EQ(P("S,1000,50"), EspMsgIgnore); // n=3 < 4

    // --- DA / ATK / LOC -----------------------------------------------------
    CHECK_INT_EQ(P("DA,a1b2c3d4e5f6,6"), EspMsgDeauthTarget);
    CHECK(mac_eq(m.u.deauth.bssid, A1F6));
    CHECK_INT_EQ(m.u.deauth.channel, 6);
    CHECK_INT_EQ(P("DA,xx,6"), EspMsgIgnore); // bad mac

    CHECK_INT_EQ(P("ATK,blespam,12"), EspMsgAttack);
    CHECK_STR_EQ(m.u.attack.kind, "blespam");
    CHECK_INT_EQ((long)m.u.attack.value, 12);
    CHECK_INT_EQ(P("ATK,probeflood"), EspMsgAttack); // n=2, no value
    CHECK_INT_EQ((long)m.u.attack.value, 0);

    CHECK_INT_EQ(P("LOC,-42"), EspMsgLocate);
    CHECK_INT_EQ(m.u.locate.rssi, -42);

    // --- banners / batch markers / junk ------------------------------------
    CHECK_INT_EQ(P("FLOCKCO,1"), EspMsgBanner);
    CHECK_INT_EQ(m.u.banner.version, 1); // wire-protocol version parsed from the banner
    CHECK_INT_EQ(P("FLOCKCO,2"), EspMsgBanner);
    CHECK_INT_EQ(m.u.banner.version, 2); // a different version still parses (app flags mismatch)
    CHECK_INT_EQ(P("FLOCKCO"), EspMsgBanner);
    CHECK_INT_EQ(m.u.banner.version, 0); // no version field (old FW) -> 0
    CHECK_INT_EQ(P("WBEGIN"), EspMsgWifiBegin);
    CHECK_INT_EQ(P("WEND"), EspMsgWifiEnd); // must NOT be mistaken for "W,"
    CHECK_INT_EQ(P("BBEGIN"), EspMsgBleBegin);
    CHECK_INT_EQ(P("BEND"), EspMsgBleEnd); // must NOT be mistaken for "BLE,"
    CHECK_INT_EQ(P("GARBAGE"), EspMsgIgnore);
    CHECK_INT_EQ(P(""), EspMsgIgnore);

#undef P
}
