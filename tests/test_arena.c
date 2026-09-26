/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/arena.h"
#include "rune/region.h"
#include "rune/status.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures += 1; \
        } \
    } while (0)

static void check_status(
    rune_status actual,
    rune_status expected,
    const char *expr,
    int line
)
{
    if (actual != expected) {
        fprintf(
            stderr,
            "%s:%d: %s: expected %s, got %s\n",
            __FILE__,
            line,
            expr,
            rune_status_name(expected),
            rune_status_name(actual)
        );
        failures += 1;
    }
}

#define CHECK_STATUS(expr, expected) \
    check_status((expr), (expected), #expr, __LINE__)

static void init_arena(
    uint8_t *bytes,
    uint64_t length,
    rune_region *region,
    rune_span *storage,
    rune_arena *arena
)
{
    CHECK_STATUS(
        rune_region_attach(
            region,
            bytes,
            length,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            storage,
            region,
            0u,
            length,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_arena_init(arena, storage), RUNE_OK);
}

static void test_status_names(void)
{
    CHECK(strcmp(
        rune_status_name(RUNE_ERR_EXHAUSTED),
        "RUNE_ERR_EXHAUSTED") == 0);
    CHECK(strcmp(
        rune_status_name(RUNE_ERR_STALE_CHECKPOINT),
        "RUNE_ERR_STALE_CHECKPOINT") == 0);
}

static void test_init_requires_read_write_storage(void)
{
    uint8_t bytes[8];
    rune_region region;
    rune_span storage;
    rune_arena arena;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            bytes,
            (uint64_t)sizeof(bytes),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            &storage,
            &region,
            0u,
            (uint64_t)sizeof(bytes),
            RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_arena_init(&arena, &storage), RUNE_ERR_ACCESS);

    CHECK_STATUS(
        rune_span_init(
            &storage,
            &region,
            0u,
            (uint64_t)sizeof(bytes),
            RUNE_ACCESS_READ
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_arena_init(&arena, &storage), RUNE_ERR_ACCESS);
}

static void test_alignment_and_accounting(void)
{
    uint8_t bytes[24];
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span a;
    rune_span b;
    rune_span empty;
    rune_span c;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            bytes,
            (uint64_t)sizeof(bytes),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            &storage,
            &region,
            4u,
            16u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_arena_init(&arena, &storage), RUNE_OK);

    CHECK_STATUS(rune_arena_alloc(&arena, 3u, 1u, &a), RUNE_OK);
    CHECK(a.offset == 4u);
    CHECK(a.length == 3u);
    CHECK(arena.cursor == 3u);
    CHECK(arena.high_water == 3u);
    CHECK(arena.cumulative_payload_bytes == 3u);
    CHECK(arena.cumulative_consumed_bytes == 3u);
    CHECK(arena.allocation_count == 1u);

    CHECK_STATUS(rune_arena_alloc(&arena, 2u, 4u, &b), RUNE_OK);
    CHECK(b.offset == 8u);
    CHECK((b.offset - arena.storage.offset) % 4u == 0u);
    CHECK(arena.cursor == 6u);
    CHECK(arena.high_water == 6u);
    CHECK(arena.cumulative_payload_bytes == 5u);
    CHECK(arena.cumulative_consumed_bytes == 6u);
    CHECK(arena.allocation_count == 2u);

    CHECK_STATUS(rune_arena_alloc(&arena, 0u, 8u, &empty), RUNE_OK);
    CHECK(empty.offset == 12u);
    CHECK(empty.length == 0u);
    CHECK(arena.cursor == 6u);
    CHECK(arena.cumulative_payload_bytes == 5u);
    CHECK(arena.cumulative_consumed_bytes == 6u);
    CHECK(arena.allocation_count == 2u);

    CHECK_STATUS(rune_arena_alloc(&arena, 8u, 8u, &c), RUNE_OK);
    CHECK(c.offset == 12u);
    CHECK(c.length == 8u);
    CHECK(arena.cursor == 16u);
    CHECK(arena.high_water == 16u);
    CHECK(arena.cumulative_payload_bytes == 13u);
    CHECK(arena.cumulative_consumed_bytes == 16u);
    CHECK(arena.allocation_count == 3u);

    CHECK_STATUS(
        rune_arena_alloc(&arena, 1u, 1u, &c),
        RUNE_ERR_EXHAUSTED
    );

    CHECK_STATUS(
        rune_arena_alloc(&arena, 1u, 0u, &c),
        RUNE_ERR_INVALID_ARGUMENT
    );
    CHECK_STATUS(
        rune_arena_alloc(&arena, 1u, 3u, &c),
        RUNE_ERR_INVALID_ARGUMENT
    );
}

static void test_exhaustion_is_non_mutating(void)
{
    uint8_t bytes[16];
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span out;
    uint64_t cursor;
    uint64_t high_water;
    uint64_t payload;
    uint64_t consumed;
    uint64_t count;

    init_arena(bytes, 16u, &region, &storage, &arena);
    CHECK_STATUS(rune_arena_alloc(&arena, 7u, 1u, &out), RUNE_OK);

    cursor = arena.cursor;
    high_water = arena.high_water;
    payload = arena.cumulative_payload_bytes;
    consumed = arena.cumulative_consumed_bytes;
    count = arena.allocation_count;

    CHECK_STATUS(
        rune_arena_alloc(&arena, 10u, 8u, &out),
        RUNE_ERR_EXHAUSTED
    );
    CHECK(arena.cursor == cursor);
    CHECK(arena.high_water == high_water);
    CHECK(arena.cumulative_payload_bytes == payload);
    CHECK(arena.cumulative_consumed_bytes == consumed);
    CHECK(arena.allocation_count == count);
}

static void test_checkpoint_restore_and_reset(void)
{
    uint8_t bytes[32];
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span first;
    rune_span second;
    rune_span reused;
    rune_arena_checkpoint first_mark;
    rune_arena_checkpoint later_mark;
    uint64_t generation;

    memset(bytes, 0, sizeof(bytes));
    init_arena(bytes, 32u, &region, &storage, &arena);

    CHECK_STATUS(rune_arena_alloc(&arena, 8u, 1u, &first), RUNE_OK);
    CHECK_STATUS(rune_arena_checkpoint_save(&arena, &first_mark), RUNE_OK);
    CHECK_STATUS(rune_arena_alloc(&arena, 8u, 1u, &second), RUNE_OK);
    CHECK_STATUS(rune_span_fill(&second, (uint8_t)0x5au), RUNE_OK);

    CHECK(arena.cursor == 16u);
    CHECK(arena.high_water == 16u);
    CHECK(arena.cumulative_consumed_bytes == 16u);

    CHECK_STATUS(
        rune_arena_checkpoint_restore(&arena, &first_mark),
        RUNE_OK
    );
    CHECK(arena.cursor == 8u);
    CHECK(arena.high_water == 16u);
    CHECK(arena.cumulative_consumed_bytes == 16u);

    CHECK_STATUS(rune_arena_alloc(&arena, 4u, 1u, &reused), RUNE_OK);
    CHECK(reused.offset == 8u);
    CHECK(arena.cursor == 12u);
    CHECK(arena.high_water == 16u);
    CHECK(arena.cumulative_payload_bytes == 20u);
    CHECK(arena.cumulative_consumed_bytes == 20u);
    CHECK(arena.allocation_count == 3u);

    CHECK_STATUS(rune_arena_checkpoint_save(&arena, &later_mark), RUNE_OK);
    CHECK_STATUS(
        rune_arena_checkpoint_restore(&arena, &first_mark),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_arena_checkpoint_restore(&arena, &later_mark),
        RUNE_ERR_STALE_CHECKPOINT
    );

    generation = arena.generation;
    CHECK_STATUS(rune_arena_reset(&arena), RUNE_OK);
    CHECK(arena.cursor == 0u);
    CHECK(arena.generation == generation + 1u);
    CHECK(arena.high_water == 16u);
    CHECK(arena.cumulative_payload_bytes == 20u);
    CHECK(arena.cumulative_consumed_bytes == 20u);
    CHECK(bytes[8] == (uint8_t)0x5au);

    CHECK_STATUS(
        rune_arena_checkpoint_restore(&arena, &first_mark),
        RUNE_ERR_STALE_CHECKPOINT
    );
}

static void test_checkpoint_owner_binding(void)
{
    uint8_t bytes_a[8];
    uint8_t bytes_b[8];
    rune_region region_a;
    rune_region region_b;
    rune_span storage_a;
    rune_span storage_b;
    rune_arena arena_a;
    rune_arena arena_b;
    rune_arena_checkpoint mark;

    init_arena(bytes_a, 8u, &region_a, &storage_a, &arena_a);
    init_arena(bytes_b, 8u, &region_b, &storage_b, &arena_b);

    CHECK_STATUS(rune_arena_checkpoint_save(&arena_a, &mark), RUNE_OK);
    CHECK_STATUS(
        rune_arena_checkpoint_restore(&arena_b, &mark),
        RUNE_ERR_STALE_CHECKPOINT
    );
}

static void test_accounting_overflow_is_non_mutating(void)
{
    uint8_t bytes[8];
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span out;

    init_arena(bytes, 8u, &region, &storage, &arena);

    arena.cumulative_payload_bytes = UINT64_MAX;
    arena.cumulative_consumed_bytes = UINT64_MAX;
    CHECK_STATUS(
        rune_arena_alloc(&arena, 1u, 1u, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK(arena.cursor == 0u);

    arena.cumulative_payload_bytes = 0u;
    arena.cumulative_consumed_bytes = 0u;
    arena.allocation_count = UINT64_MAX;
    CHECK_STATUS(
        rune_arena_alloc(&arena, 1u, 1u, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK(arena.cursor == 0u);
}

static void test_reset_generation_overflow(void)
{
    uint8_t bytes[8];
    rune_region region;
    rune_span storage;
    rune_arena arena;

    init_arena(bytes, 8u, &region, &storage, &arena);

    arena.generation = UINT64_MAX;
    CHECK_STATUS(rune_arena_reset(&arena), RUNE_ERR_OVERFLOW);
    CHECK(arena.cursor == 0u);
    CHECK(arena.generation == UINT64_MAX);
}


static void test_reinitialize_invalidates_old_checkpoint(void)
{
    uint8_t bytes_a[16];
    uint8_t bytes_b[16];
    rune_region region_a;
    rune_region region_b;
    rune_span storage_a;
    rune_span storage_b;
    rune_arena arena;
    rune_arena_checkpoint old_mark;
    rune_span first;
    rune_span current;
    uint64_t old_incarnation;

    init_arena(bytes_a, 16u, &region_a, &storage_a, &arena);
    CHECK_STATUS(rune_arena_alloc(&arena, 4u, 1u, &first), RUNE_OK);
    CHECK_STATUS(rune_arena_checkpoint_save(&arena, &old_mark), RUNE_OK);
    old_incarnation = arena.incarnation;

    CHECK_STATUS(
        rune_region_attach(
            &region_b,
            bytes_b,
            16u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            &storage_b,
            &region_b,
            0u,
            16u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_arena_init(&arena, &storage_b), RUNE_OK);
    CHECK(arena.incarnation != old_incarnation);

    CHECK_STATUS(rune_arena_alloc(&arena, 8u, 1u, &current), RUNE_OK);
    CHECK(arena.cursor == 8u);
    CHECK_STATUS(
        rune_arena_checkpoint_restore(&arena, &old_mark),
        RUNE_ERR_STALE_CHECKPOINT
    );
    CHECK(arena.cursor == 8u);
}

static void test_allocation_output_cannot_alias_storage_descriptor(void)
{
    uint8_t bytes[16];
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span original_storage;
    uint64_t cursor;
    uint64_t high_water;
    uint64_t payload;
    uint64_t consumed;
    uint64_t count;

    init_arena(bytes, 16u, &region, &storage, &arena);
    original_storage = arena.storage;
    cursor = arena.cursor;
    high_water = arena.high_water;
    payload = arena.cumulative_payload_bytes;
    consumed = arena.cumulative_consumed_bytes;
    count = arena.allocation_count;

    CHECK_STATUS(
        rune_arena_alloc(&arena, 4u, 1u, &arena.storage),
        RUNE_ERR_INVALID_ARGUMENT
    );
    CHECK(arena.storage.region == original_storage.region);
    CHECK(arena.storage.offset == original_storage.offset);
    CHECK(arena.storage.length == original_storage.length);
    CHECK(arena.storage.access == original_storage.access);
    CHECK(arena.cursor == cursor);
    CHECK(arena.high_water == high_water);
    CHECK(arena.cumulative_payload_bytes == payload);
    CHECK(arena.cumulative_consumed_bytes == consumed);
    CHECK(arena.allocation_count == count);
}

static void test_zero_capacity_arena(void)
{
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span empty;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            NULL,
            0u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            &storage,
            &region,
            0u,
            0u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_arena_init(&arena, &storage), RUNE_OK);
    CHECK_STATUS(rune_arena_alloc(&arena, 0u, 1u, &empty), RUNE_OK);
    CHECK_STATUS(
        rune_arena_alloc(&arena, 1u, 1u, &empty),
        RUNE_ERR_EXHAUSTED
    );
}

int main(void)
{
    test_status_names();
    test_init_requires_read_write_storage();
    test_alignment_and_accounting();
    test_exhaustion_is_non_mutating();
    test_checkpoint_restore_and_reset();
    test_checkpoint_owner_binding();
    test_reinitialize_invalidates_old_checkpoint();
    test_allocation_output_cannot_alias_storage_descriptor();
    test_accounting_overflow_is_non_mutating();
    test_reset_generation_overflow();
    test_zero_capacity_arena();

    if (failures != 0) {
        fprintf(stderr, "RUNE R2 arena tests: FAIL (%d)\n", failures);
        return 1;
    }

    puts("RUNE R2 arena tests: PASS");
    return 0;
}
