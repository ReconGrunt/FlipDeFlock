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
MODULES := ../helpers/flock_db.c ../helpers/watchscore.c ../helpers/wifi_audit.c \
           ../helpers/esp_parser.c
MOCKS   := mocks/furi_string_mock.c
TESTS   := test_flock_db.c test_watchscore.c test_wifi_audit.c test_esp_parser.c test_main.c
HEADERS := test.h mocks/core/string.h

BIN := run_tests

.PHONY: test build clean
test: build
	./$(BIN)

build: $(BIN)

$(BIN): $(MODULES) $(MOCKS) $(TESTS) $(HEADERS)
	$(CC) $(CFLAGS) $(MODULES) $(MOCKS) $(TESTS) -o $(BIN)

clean:
	rm -f $(BIN)
