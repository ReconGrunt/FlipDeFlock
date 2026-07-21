// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt and FlipDeFlock contributors
//
// Host unit-test runner for FlipDeFlock's pure-logic modules. Builds with plain
// gcc (see Makefile) so the confidence-scoring / coincidence-gate / auth-grading
// contracts can be regression-tested off-device. Exits non-zero on any failure.
#include <stdio.h>

int g_checks = 0;
int g_fails = 0;

void suite_flock_db(void);
void suite_watchscore(void);
void suite_wifi_audit(void);
void suite_esp_parser(void);

int main(void) {
    printf("FlipDeFlock host unit tests\n");
    suite_flock_db();
    suite_watchscore();
    suite_wifi_audit();
    suite_esp_parser();

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    if(g_fails) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
