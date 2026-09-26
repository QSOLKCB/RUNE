/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/region.h"
#include "rune/status.h"

#include <stddef.h>
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

static void init_sequence(uint8_t *storage, size_t length)
{
    size_t i;

    for (i = 0u; i < length; ++i) {
        storage[i] = (uint8_t)i;
    }
}

static void test_status_names(void)
{
    CHECK(strcmp(rune_status_name(RUNE_OK), "RUNE_OK") == 0);
    CHECK(strcmp(rune_status_name((rune_status)999), "RUNE_STATUS_UNKNOWN") == 0);
}

static void test_region_attach(void)
{
    uint8_t storage[16];
    rune_region region;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK(region.data == storage);
    CHECK(region.capacity == (uint64_t)sizeof(storage));
    CHECK(region.access == (RUNE_ACCESS_READ | RUNE_ACCESS_WRITE));

    CHECK_STATUS(
        rune_region_attach(&region, NULL, 1u, RUNE_ACCESS_READ),
        RUNE_ERR_NULL_ARGUMENT
    );
    CHECK_STATUS(
        rune_region_attach(&region, NULL, 0u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_region_attach(&region, storage, 1u, 0u),
        RUNE_ERR_INVALID_ARGUMENT
    );
    CHECK_STATUS(
        rune_region_attach(&region, storage, 1u, (uint32_t)4u),
        RUNE_ERR_INVALID_ARGUMENT
    );

    if (sizeof(size_t) < sizeof(uint64_t)) {
        CHECK_STATUS(
            rune_region_attach(
                &region,
                storage,
                (uint64_t)((size_t)-1) + (uint64_t)1u,
                RUNE_ACCESS_READ
            ),
            RUNE_ERR_UNSUPPORTED_CAPACITY
        );
    }
}

static void test_span_bounds_and_access(void)
{
    uint8_t storage[16];
    rune_region region;
    rune_region read_only;
    rune_span span;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );

    CHECK_STATUS(
        rune_span_init(
            &span,
            &region,
            4u,
            8u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK(span.offset == 4u);
    CHECK(span.length == 8u);

    CHECK_STATUS(
        rune_span_init(&span, &region, 16u, 0u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&span, &region, 17u, 0u, RUNE_ACCESS_READ),
        RUNE_ERR_OUT_OF_BOUNDS
    );
    CHECK_STATUS(
        rune_span_init(&span, &region, UINT64_MAX, 2u, RUNE_ACCESS_READ),
        RUNE_ERR_OVERFLOW
    );

    CHECK_STATUS(
        rune_region_attach(
            &read_only,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&span, &read_only, 0u, 1u, RUNE_ACCESS_WRITE),
        RUNE_ERR_ACCESS
    );
}

static void test_span_slice(void)
{
    uint8_t storage[16];
    rune_region region;
    rune_span parent;
    rune_span child;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            &parent,
            &region,
            4u,
            8u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );

    CHECK_STATUS(
        rune_span_slice(&child, &parent, 2u, 4u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK(child.region == &region);
    CHECK(child.offset == 6u);
    CHECK(child.length == 4u);
    CHECK(child.access == RUNE_ACCESS_READ);

    CHECK_STATUS(
        rune_span_slice(&child, &parent, 8u, 0u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_slice(&child, &parent, 9u, 0u, RUNE_ACCESS_READ),
        RUNE_ERR_OUT_OF_BOUNDS
    );
    CHECK_STATUS(
        rune_span_slice(
            &child,
            &parent,
            UINT64_MAX,
            2u,
            RUNE_ACCESS_READ
        ),
        RUNE_ERR_OVERFLOW
    );
    CHECK_STATUS(
        rune_span_slice(&child, &parent, 0u, 1u, (uint32_t)4u),
        RUNE_ERR_INVALID_ARGUMENT
    );
}

static void test_overlap_move(void)
{
    uint8_t storage[16];
    uint8_t expected_forward[16] = {
        0u, 1u, 2u, 3u, 0u, 1u, 2u, 3u,
        4u, 5u, 6u, 7u, 12u, 13u, 14u, 15u
    };
    uint8_t expected_backward[16] = {
        4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
    };
    rune_region region;
    rune_span src;
    rune_span dst;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );

    init_sequence(storage, sizeof(storage));
    CHECK_STATUS(
        rune_span_init(&src, &region, 0u, 8u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&dst, &region, 4u, 8u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );
    CHECK_STATUS(rune_span_move(&dst, &src), RUNE_OK);
    CHECK(memcmp(storage, expected_forward, sizeof(storage)) == 0);

    init_sequence(storage, sizeof(storage));
    CHECK_STATUS(
        rune_span_init(&src, &region, 4u, 8u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&dst, &region, 0u, 8u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );
    CHECK_STATUS(rune_span_move(&dst, &src), RUNE_OK);
    CHECK(memcmp(storage, expected_backward, sizeof(storage)) == 0);
}

static void test_move_failures_are_non_mutating(void)
{
    uint8_t storage[16];
    uint8_t before[16];
    rune_region region;
    rune_span src;
    rune_span dst;

    init_sequence(storage, sizeof(storage));
    memcpy(before, storage, sizeof(storage));

    CHECK_STATUS(
        rune_region_attach(
            &region,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&src, &region, 0u, 4u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&dst, &region, 8u, 3u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );
    CHECK_STATUS(rune_span_move(&dst, &src), RUNE_ERR_CAPACITY);
    CHECK(memcmp(storage, before, sizeof(storage)) == 0);

    CHECK_STATUS(
        rune_span_init(&src, &region, 0u, 4u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&dst, &region, 8u, 4u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );
    CHECK_STATUS(rune_span_move(&dst, &src), RUNE_ERR_ACCESS);
    CHECK(memcmp(storage, before, sizeof(storage)) == 0);

    CHECK_STATUS(
        rune_span_init(&src, &region, 0u, 4u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&dst, &region, 8u, 4u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(rune_span_move(&dst, &src), RUNE_ERR_ACCESS);
    CHECK(memcmp(storage, before, sizeof(storage)) == 0);
}

static void test_fill_and_clear(void)
{
    uint8_t storage[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t expected_fill[8] = {
        1u, 2u, 0xa5u, 0xa5u, 0xa5u, 0xa5u, 7u, 8u
    };
    uint8_t expected_clear[8] = {
        1u, 2u, 0u, 0u, 0u, 0u, 7u, 8u
    };
    rune_region region;
    rune_span span;

    CHECK_STATUS(
        rune_region_attach(
            &region,
            storage,
            (uint64_t)sizeof(storage),
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&span, &region, 2u, 4u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );

    CHECK_STATUS(rune_span_fill(&span, (uint8_t)0xa5u), RUNE_OK);
    CHECK(memcmp(storage, expected_fill, sizeof(storage)) == 0);

    CHECK_STATUS(rune_span_clear(&span), RUNE_OK);
    CHECK(memcmp(storage, expected_clear, sizeof(storage)) == 0);
}

static void test_zero_length_null_region(void)
{
    rune_region region;
    rune_span src;
    rune_span dst;

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
        rune_span_init(&src, &region, 0u, 0u, RUNE_ACCESS_READ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(&dst, &region, 0u, 0u, RUNE_ACCESS_WRITE),
        RUNE_OK
    );
    CHECK_STATUS(rune_span_move(&dst, &src), RUNE_OK);
    CHECK_STATUS(rune_span_fill(&dst, (uint8_t)0xffu), RUNE_OK);
    CHECK_STATUS(rune_span_clear(&dst), RUNE_OK);
}

int main(void)
{
    test_status_names();
    test_region_attach();
    test_span_bounds_and_access();
    test_span_slice();
    test_overlap_move();
    test_move_failures_are_non_mutating();
    test_fill_and_clear();
    test_zero_length_null_region();

    if (failures != 0) {
        fprintf(stderr, "RUNE R1 region tests: FAIL (%d)\n", failures);
        return 1;
    }

    puts("RUNE R1 region tests: PASS");
    return 0;
}
