CC ?= cc
AR ?= ar
CPPFLAGS ?=
CFLAGS ?=

STRICT_CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic
INCLUDES = -Iinclude

BUILD_DIR = build
LIB = $(BUILD_DIR)/librune.a
OBJS = $(BUILD_DIR)/status.o $(BUILD_DIR)/region.o $(BUILD_DIR)/arena.o $(BUILD_DIR)/numeric.o
TEST_REGION = $(BUILD_DIR)/test_region
TEST_ARENA = $(BUILD_DIR)/test_arena
TEST_NUMERIC = $(BUILD_DIR)/test_numeric
CORPUS = $(BUILD_DIR)/rune_corpus

.PHONY: all test corpus-smoke clean

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/status.o: src/status.c include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/region.o: src/region.c include/rune/region.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/arena.o: src/arena.c include/rune/arena.h include/rune/region.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/numeric.o: src/numeric.c include/rune/numeric.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(TEST_REGION): tests/test_region.c $(LIB) include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_region.c $(LIB) -o $@

$(TEST_ARENA): tests/test_arena.c $(LIB) include/rune/arena.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_arena.c $(LIB) -o $@

$(TEST_NUMERIC): tests/test_numeric.c $(LIB) include/rune/numeric.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_numeric.c $(LIB) -o $@

$(CORPUS): corpus/rune_corpus.c $(LIB) include/rune/arena.h include/rune/numeric.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) corpus/rune_corpus.c $(LIB) -o $@

corpus-smoke: $(CORPUS)
	./$(CORPUS) --profile smoke > $(BUILD_DIR)/corpus-smoke.jsonl
	test "`wc -l < $(BUILD_DIR)/corpus-smoke.jsonl`" -eq 17
	grep -Fxq '{"contract":"rune.corpus.summary.v1","profile":"smoke","working_set_bytes":32768,"seed":303,"receipt_count":16,"fingerprint_u64":7795391999286618454}' $(BUILD_DIR)/corpus-smoke.jsonl
	if ./$(CORPUS) --bytes -18446744073709547520 >/dev/null 2>&1; then \
		echo "negative --bytes unexpectedly accepted" >&2; \
		exit 1; \
	fi

test: $(TEST_REGION) $(TEST_ARENA) $(TEST_NUMERIC)
	./$(TEST_REGION)
	./$(TEST_ARENA)
	./$(TEST_NUMERIC)

clean:
	rm -rf $(BUILD_DIR)
