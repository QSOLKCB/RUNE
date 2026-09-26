CC ?= cc
AR ?= ar
CPPFLAGS ?=
CFLAGS ?=

STRICT_CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic
INCLUDES = -Iinclude

BUILD_DIR = build
LIB = $(BUILD_DIR)/librune.a
OBJS = $(BUILD_DIR)/status.o $(BUILD_DIR)/region.o $(BUILD_DIR)/arena.o
TEST_REGION = $(BUILD_DIR)/test_region
TEST_ARENA = $(BUILD_DIR)/test_arena

.PHONY: all test clean

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/status.o: src/status.c include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/region.o: src/region.c include/rune/region.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/arena.o: src/arena.c include/rune/arena.h include/rune/region.h include/rune/status.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(TEST_REGION): tests/test_region.c $(LIB) include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_region.c $(LIB) -o $@

$(TEST_ARENA): tests/test_arena.c $(LIB) include/rune/arena.h include/rune/region.h include/rune/status.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT_CFLAGS) $(INCLUDES) tests/test_arena.c $(LIB) -o $@

test: $(TEST_REGION) $(TEST_ARENA)
	./$(TEST_REGION)
	./$(TEST_ARENA)

clean:
	rm -rf $(BUILD_DIR)
