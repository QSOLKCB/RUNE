CC ?= cc
AR ?= ar
CPPFLAGS ?=
CFLAGS ?=

STRICT_CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic
INCLUDES = -Iinclude

BUILD_DIR = build
LIB = $(BUILD_DIR)/librune.a
OBJS = $(BUILD_DIR)/status.o $(BUILD_DIR)/region.o $(BUILD_DIR)/arena.o $(BUILD_DIR)/numeric.o $(BUILD_DIR)/ring.o $(BUILD_DIR)/operation.o
TEST_REGION = $(BUILD_DIR)/test_region
TEST_ARENA = $(BUILD_DIR)/test_arena
TEST_NUMERIC = $(BUILD_DIR)/test_numeric
TEST_RING = $(BUILD_DIR)/test_ring
TEST_OPERATION = $(BUILD_DIR)/test_operation
R6_PROOF = $(BUILD_DIR)/rune_r6_proof
R7_STUDY = $(BUILD_DIR)/rune_r7_study
R7_CLOCK_REGRESSION = $(BUILD_DIR)/test_r7_clock
R7_FLOAT_CLOCK_REGRESSION = $(BUILD_DIR)/test_r7_clock_float
CORPUS = $(BUILD_DIR)/rune_corpus

.PHONY: all test r6-proof r7-study-smoke r7-study r7-clock-regression r7-evidence-regression corpus-smoke clean

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

$(BUILD_DIR)/ring.o: src/ring.c include/rune/ring.h include/rune/region.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/operation.o: src/operation.c include/rune/operation.h include/rune/region.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(TEST_REGION): tests/test_region.c $(LIB) include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_region.c $(LIB) -o $@

$(TEST_ARENA): tests/test_arena.c $(LIB) include/rune/arena.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_arena.c $(LIB) -o $@

$(TEST_NUMERIC): tests/test_numeric.c $(LIB) include/rune/numeric.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_numeric.c $(LIB) -o $@

$(TEST_RING): tests/test_ring.c $(LIB) include/rune/ring.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_ring.c $(LIB) -o $@

$(TEST_OPERATION): tests/test_operation.c $(LIB) include/rune/operation.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_operation.c $(LIB) -o $@

$(R6_PROOF): proof/r6_proof.c $(LIB) include/rune/operation.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) proof/r6_proof.c $(LIB) -o $@

r6-proof: $(R6_PROOF)
	./$(R6_PROOF) > $(BUILD_DIR)/r6-proof.tsv
	cmp $(BUILD_DIR)/r6-proof.tsv proof/r6-proof-receipts.v1.tsv

$(R7_STUDY): study/r7_memory_wall.c $(LIB) include/rune/arena.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) study/r7_memory_wall.c $(LIB) -o $@

r7-study-smoke: $(R7_STUDY)
	./$(R7_STUDY) --bytes 32768 --repeats 1 > $(BUILD_DIR)/r7-study-smoke.tsv
	test "`wc -l < $(BUILD_DIR)/r7-study-smoke.tsv`" -eq 20
	awk -F '\t' 'NF != 17 { exit 1 } END { if (NR != 20) exit 1 }' $(BUILD_DIR)/r7-study-smoke.tsv

r7-study: $(R7_STUDY)
	./$(R7_STUDY) --profile local --repeats 5

$(R7_CLOCK_REGRESSION): tests/test_r7_clock.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -fsanitize=undefined -fno-sanitize-recover=undefined tests/test_r7_clock.c $(LIB) -o $@

$(R7_FLOAT_CLOCK_REGRESSION): tests/test_r7_clock_float.c tests/r7_float_clock/time.h $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) -Itests/r7_float_clock $(INCLUDES) -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=undefined,float-cast-overflow tests/test_r7_clock_float.c $(LIB) -o $@

r7-clock-regression: $(R7_CLOCK_REGRESSION) $(R7_FLOAT_CLOCK_REGRESSION)
	./$(R7_CLOCK_REGRESSION)
	./$(R7_FLOAT_CLOCK_REGRESSION)

r7-evidence-regression:
	@r7_cc='$(CC)'; \
	if [ "$$r7_cc" = cc ]; then r7_cc=/usr/bin/cc; fi; \
	CC="$$r7_cc" AR="$(AR)" sh tests/test_r7_run_local.sh

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
	if ./$(CORPUS) --bytes ' -18446744073709547520' >/dev/null 2>&1; then \
		echo "whitespace-prefixed negative --bytes unexpectedly accepted" >&2; \
		exit 1; \
	fi
	if ./$(CORPUS) --seed ' -1' >/dev/null 2>&1; then \
		echo "whitespace-prefixed negative --seed unexpectedly accepted" >&2; \
		exit 1; \
	fi

test: $(TEST_REGION) $(TEST_ARENA) $(TEST_NUMERIC) $(TEST_RING) $(TEST_OPERATION)
	./$(TEST_REGION)
	./$(TEST_ARENA)
	./$(TEST_NUMERIC)
	./$(TEST_RING)
	./$(TEST_OPERATION)

clean:
	rm -rf $(BUILD_DIR)
