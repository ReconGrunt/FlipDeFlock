# Host unit tests for FlipDeFlock's pure-logic modules (plain gcc, no firmware).
#
#   make -C test         build + run (default)
#   make -C test build   build only
#   make -C test clean
#
# CI runs `make -C test` on a stock ubuntu gcc; the binary exits non-zero on any
# failed check, so it gates the workflow.

CC     ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra
CFLAGS += -Imocks -I../helpers

# Pure-logic modules under test, compiled straight from the app tree.
MODULES := ../helpers/flock_db.c ../helpers/watchscore.c \
           ../helpers/esp_parser.c ../helpers/detect_rules.c ../helpers/report_escape.c \
           ../helpers/gps_parser.c ../helpers/gps_rpc_convert.c \
           ../helpers/report_fmt.c ../helpers/flock_store.c \
           ../helpers/flock_ble.c ../helpers/oui_vendor.c ../helpers/marauder_scan.c \
           ../helpers/fast_trig.c
MOCKS   := mocks/furi_string_mock.c
TESTS   := test_flock_db.c test_watchscore.c test_esp_parser.c \
           test_detect_rules.c test_report_escape.c test_gps_parser.c \
           test_gps_rpc_convert.c test_report_fmt.c \
           test_flock_store.c test_flock_ble.c test_oui_vendor.c test_marauder_scan.c \
           test_fast_trig.c test_tracker_rules.c test_main.c
HEADERS := test.h mocks/core/string.h ../helpers/tracker_rules.h \
           ../esp32_companion/flock_companion/tracker_rules.h

BIN := run_tests

.PHONY: test build clean
test: build
	./$(BIN)

build: $(BIN)

# -lm: sqrtf/fabsf, plus test_fast_trig compares against libm's own sinf/cosf.
$(BIN): $(MODULES) $(MOCKS) $(TESTS) $(HEADERS)
	$(CC) $(CFLAGS) $(MODULES) $(MOCKS) $(TESTS) -o $(BIN) -lm

clean:
	rm -f $(BIN)
