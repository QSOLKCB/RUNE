/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/region.h"
#include "rune/ring.h"
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

static void init_ring(
    uint8_t *bytes,
    uint64_t capacity,
    rune_region *region,
    rune_span *storage,
    rune_ring *ring
)
{
    CHECK_STATUS(
        rune_region_attach(
            region,
            bytes,
            capacity,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_span_init(
            storage,
            region,
            0u,
            capacity,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_ring_init(ring, storage), RUNE_OK);
}

static void write_view_bytes(
    const rune_ring_views *views,
    const uint8_t *source,
    uint64_t length
)
{
    uint64_t first_take;
    uint64_t second_take;
    uint64_t i;

    CHECK(length <= views->total_length);

    first_take = views->first.length;
    if (first_take > length) {
        first_take = length;
    }

    for (i = 0u; i < first_take; ++i) {
        views->first.region->data[
            (size_t)(views->first.offset + i)
        ] = source[(size_t)i];
    }

    second_take = length - first_take;
    CHECK(second_take <= views->second.length);
    for (i = 0u; i < second_take; ++i) {
        views->second.region->data[
            (size_t)(views->second.offset + i)
        ] = source[(size_t)(first_take + i)];
    }
}

static void read_view_bytes(
    const rune_ring_views *views,
    uint8_t *destination,
    uint64_t length
)
{
    uint64_t first_take;
    uint64_t second_take;
    uint64_t i;

    CHECK(length <= views->total_length);

    first_take = views->first.length;
    if (first_take > length) {
        first_take = length;
    }

    for (i = 0u; i < first_take; ++i) {
        destination[(size_t)i] = views->first.region->data[
            (size_t)(views->first.offset + i)
        ];
    }

    second_take = length - first_take;
    CHECK(second_take <= views->second.length);
    for (i = 0u; i < second_take; ++i) {
        destination[(size_t)(first_take + i)] =
            views->second.region->data[
                (size_t)(views->second.offset + i)
            ];
    }
}

static void test_init_requires_read_write_storage(void)
{
    uint8_t bytes[8];
    rune_region region;
    rune_span storage;
    rune_ring ring;

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
            RUNE_ACCESS_READ
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_ring_init(&ring, &storage), RUNE_ERR_ACCESS);

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
    CHECK_STATUS(rune_ring_init(&ring, &storage), RUNE_ERR_ACCESS);
}

static void test_initial_and_zero_length_views(void)
{
    uint8_t bytes[12];
    rune_region region;
    rune_span storage;
    rune_ring ring;
    rune_ring_views read_views;
    rune_ring_views write_views;

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
            2u,
            8u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        ),
        RUNE_OK
    );
    CHECK_STATUS(rune_ring_init(&ring, &storage), RUNE_OK);

    CHECK(ring.storage.offset == 2u);
    CHECK(ring.storage.length == 8u);
    CHECK(ring.read_cursor == 0u);
    CHECK(ring.write_cursor == 0u);
    CHECK(ring.size == 0u);

    CHECK_STATUS(
        rune_ring_read_views(&ring, &read_views),
        RUNE_OK
    );
    CHECK(read_views.total_length == 0u);
    CHECK(read_views.first.offset == 2u);
    CHECK(read_views.first.length == 0u);
    CHECK(read_views.second.length == 0u);
    CHECK(read_views.first.access == RUNE_ACCESS_READ);

    CHECK_STATUS(
        rune_ring_write_views(&ring, &write_views),
        RUNE_OK
    );
    CHECK(write_views.total_length == 8u);
    CHECK(write_views.first.offset == 2u);
    CHECK(write_views.first.length == 8u);
    CHECK(write_views.second.length == 0u);
    CHECK(write_views.first.access == RUNE_ACCESS_WRITE);

    CHECK_STATUS(rune_ring_produce(&ring, 0u), RUNE_OK);
    CHECK_STATUS(rune_ring_consume(&ring, 0u), RUNE_OK);
}

static void test_wrap_and_fifo_order(void)
{
    uint8_t bytes[8] = { 0u };
    const uint8_t first_batch[6] = {
        1u, 2u, 3u, 4u, 5u, 6u
    };
    const uint8_t second_batch[6] = {
        7u, 8u, 9u, 10u, 11u, 12u
    };
    const uint8_t expected[8] = {
        5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u
    };
    uint8_t observed[8] = { 0u };
    rune_region region;
    rune_span storage;
    rune_ring ring;
    rune_ring_views views;

    init_ring(
        bytes,
        (uint64_t)sizeof(bytes),
        &region,
        &storage,
        &ring
    );

    CHECK_STATUS(rune_ring_write_views(&ring, &views), RUNE_OK);
    write_view_bytes(&views, first_batch, 6u);
    CHECK_STATUS(rune_ring_produce(&ring, 6u), RUNE_OK);

    CHECK(ring.read_cursor == 0u);
    CHECK(ring.write_cursor == 6u);
    CHECK(ring.size == 6u);

    CHECK_STATUS(rune_ring_consume(&ring, 4u), RUNE_OK);
    CHECK(ring.read_cursor == 4u);
    CHECK(ring.write_cursor == 6u);
    CHECK(ring.size == 2u);

    CHECK_STATUS(rune_ring_write_views(&ring, &views), RUNE_OK);
    CHECK(views.total_length == 6u);
    CHECK(views.first.offset == 6u);
    CHECK(views.first.length == 2u);
    CHECK(views.second.offset == 0u);
    CHECK(views.second.length == 4u);

    write_view_bytes(&views, second_batch, 6u);
    CHECK_STATUS(rune_ring_produce(&ring, 6u), RUNE_OK);
    CHECK(ring.size == 8u);
    CHECK(ring.write_cursor == 4u);

    CHECK_STATUS(rune_ring_read_views(&ring, &views), RUNE_OK);
    CHECK(views.total_length == 8u);
    CHECK(views.first.offset == 4u);
    CHECK(views.first.length == 4u);
    CHECK(views.second.offset == 0u);
    CHECK(views.second.length == 4u);

    read_view_bytes(&views, observed, 8u);
    CHECK(memcmp(observed, expected, sizeof(expected)) == 0);

    CHECK_STATUS(rune_ring_consume(&ring, 8u), RUNE_OK);
    CHECK(ring.size == 0u);
    CHECK(ring.read_cursor == 4u);
    CHECK(ring.write_cursor == 4u);

    CHECK_STATUS(rune_ring_write_views(&ring, &views), RUNE_OK);
    CHECK(views.first.offset == 4u);
    CHECK(views.first.length == 4u);
    CHECK(views.second.offset == 0u);
    CHECK(views.second.length == 4u);
}

static void test_commit_failures_are_non_mutating(void)
{
    uint8_t bytes[8] = { 0u };
    rune_region region;
    rune_span storage;
    rune_ring ring;
    rune_ring before;

    init_ring(
        bytes,
        (uint64_t)sizeof(bytes),
        &region,
        &storage,
        &ring
    );

    before = ring;
    CHECK_STATUS(
        rune_ring_produce(&ring, 9u),
        RUNE_ERR_CAPACITY
    );
    CHECK(memcmp(&ring, &before, sizeof(ring)) == 0);

    CHECK_STATUS(rune_ring_produce(&ring, 5u), RUNE_OK);
    before = ring;
    CHECK_STATUS(
        rune_ring_consume(&ring, 6u),
        RUNE_ERR_OUT_OF_BOUNDS
    );
    CHECK(memcmp(&ring, &before, sizeof(ring)) == 0);

    CHECK_STATUS(rune_ring_produce(&ring, 3u), RUNE_OK);
    before = ring;
    CHECK_STATUS(
        rune_ring_produce(&ring, 1u),
        RUNE_ERR_CAPACITY
    );
    CHECK(memcmp(&ring, &before, sizeof(ring)) == 0);
}

static void test_invalid_state_is_rejected(void)
{
    uint8_t bytes[8] = { 0u };
    rune_region region;
    rune_span storage;
    rune_ring ring;
    rune_ring_views views;

    init_ring(
        bytes,
        (uint64_t)sizeof(bytes),
        &region,
        &storage,
        &ring
    );

    ring.write_cursor = 1u;
    CHECK_STATUS(
        rune_ring_read_views(&ring, &views),
        RUNE_ERR_INVALID_ARGUMENT
    );

    ring.write_cursor = 0u;
    ring.size = 9u;
    CHECK_STATUS(
        rune_ring_write_views(&ring, &views),
        RUNE_ERR_INVALID_ARGUMENT
    );
}

static void test_zero_capacity_ring(void)
{
    rune_region region;
    rune_span storage;
    rune_ring ring;
    rune_ring_views views;

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
    CHECK_STATUS(rune_ring_init(&ring, &storage), RUNE_OK);

    CHECK_STATUS(rune_ring_read_views(&ring, &views), RUNE_OK);
    CHECK(views.total_length == 0u);
    CHECK_STATUS(rune_ring_write_views(&ring, &views), RUNE_OK);
    CHECK(views.total_length == 0u);

    CHECK_STATUS(rune_ring_produce(&ring, 0u), RUNE_OK);
    CHECK_STATUS(rune_ring_consume(&ring, 0u), RUNE_OK);
    CHECK_STATUS(
        rune_ring_produce(&ring, 1u),
        RUNE_ERR_CAPACITY
    );
    CHECK_STATUS(
        rune_ring_consume(&ring, 1u),
        RUNE_ERR_OUT_OF_BOUNDS
    );
}

static uint8_t stream_byte(uint64_t index)
{
    return (uint8_t)(
        (index * UINT64_C(37) + UINT64_C(11)) &
        UINT64_C(0xff)
    );
}

static void test_logical_stream_exceeds_resident_state(void)
{
    enum { RING_CAPACITY = 257 };
    const uint64_t logical_bytes = UINT64_C(1048699);
    uint8_t bytes[RING_CAPACITY];
    rune_region region;
    rune_span storage;
    rune_ring ring;
    rune_ring_views views;
    uint64_t produced;
    uint64_t consumed;
    uint64_t expected_sum;
    uint64_t observed_sum;
    uint64_t max_resident;

    produced = 0u;
    consumed = 0u;
    expected_sum = 0u;
    observed_sum = 0u;
    max_resident = 0u;

    init_ring(
        bytes,
        (uint64_t)sizeof(bytes),
        &region,
        &storage,
        &ring
    );

    while (consumed < logical_bytes) {
        if (produced < logical_bytes && ring.size < ring.storage.length) {
            uint64_t remaining;
            uint64_t take;
            uint64_t first_take;
            uint64_t second_take;
            uint64_t i;

            CHECK_STATUS(
                rune_ring_write_views(&ring, &views),
                RUNE_OK
            );

            remaining = logical_bytes - produced;
            take = UINT64_C(113) + (produced % UINT64_C(37));
            if (take > remaining) {
                take = remaining;
            }
            if (take > views.total_length) {
                take = views.total_length;
            }

            first_take = views.first.length;
            if (first_take > take) {
                first_take = take;
            }

            for (i = 0u; i < first_take; ++i) {
                uint8_t value;

                value = stream_byte(produced + i);
                views.first.region->data[
                    (size_t)(views.first.offset + i)
                ] = value;
                expected_sum += (uint64_t)value;
            }

            second_take = take - first_take;
            for (i = 0u; i < second_take; ++i) {
                uint8_t value;

                value = stream_byte(produced + first_take + i);
                views.second.region->data[
                    (size_t)(views.second.offset + i)
                ] = value;
                expected_sum += (uint64_t)value;
            }

            CHECK_STATUS(rune_ring_produce(&ring, take), RUNE_OK);
            produced += take;

            if (ring.size > max_resident) {
                max_resident = ring.size;
            }
        }

        if (ring.size != 0u) {
            uint64_t take;
            uint64_t first_take;
            uint64_t second_take;
            uint64_t i;

            CHECK_STATUS(
                rune_ring_read_views(&ring, &views),
                RUNE_OK
            );

            take = UINT64_C(79) + (consumed % UINT64_C(41));
            if (take > views.total_length) {
                take = views.total_length;
            }

            first_take = views.first.length;
            if (first_take > take) {
                first_take = take;
            }

            for (i = 0u; i < first_take; ++i) {
                observed_sum += (uint64_t)
                    views.first.region->data[
                        (size_t)(views.first.offset + i)
                    ];
            }

            second_take = take - first_take;
            for (i = 0u; i < second_take; ++i) {
                observed_sum += (uint64_t)
                    views.second.region->data[
                        (size_t)(views.second.offset + i)
                    ];
            }

            CHECK_STATUS(rune_ring_consume(&ring, take), RUNE_OK);
            consumed += take;
        }
    }

    CHECK(produced == logical_bytes);
    CHECK(consumed == logical_bytes);
    CHECK(ring.size == 0u);
    CHECK(max_resident <= (uint64_t)RING_CAPACITY);
    CHECK(logical_bytes > (uint64_t)RING_CAPACITY);
    CHECK(expected_sum == observed_sum);
}

int main(void)
{
    test_init_requires_read_write_storage();
    test_initial_and_zero_length_views();
    test_wrap_and_fifo_order();
    test_commit_failures_are_non_mutating();
    test_invalid_state_is_rejected();
    test_zero_capacity_ring();
    test_logical_stream_exceeds_resident_state();

    if (failures != 0) {
        fprintf(stderr, "RUNE R5 ring tests: FAIL (%d)\n", failures);
        return 1;
    }

    puts("RUNE R5 ring tests: PASS");
    return 0;
}
