/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/operation.h"
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

static rune_operation_span_ref span_ref(
    uint32_t slot,
    uint64_t offset,
    uint64_t length
)
{
    rune_operation_span_ref ref;

    ref.region_slot = slot;
    ref.offset = offset;
    ref.length = length;
    return ref;
}

static void attach_region(
    rune_region *region,
    uint8_t *bytes,
    uint64_t length
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
}

static void check_receipt_equal(
    const rune_operation_receipt *left,
    const rune_operation_receipt *right
)
{
    CHECK(left->operation_id == right->operation_id);
    CHECK(left->semantic_version == right->semantic_version);
    CHECK(left->status == right->status);
    CHECK(left->execution_class == right->execution_class);
    CHECK(left->descriptor_identity == right->descriptor_identity);
    CHECK(left->result_identity == right->result_identity);
    CHECK(left->bytes_read == right->bytes_read);
    CHECK(left->bytes_written == right->bytes_written);
    CHECK(left->scratch_required == right->scratch_required);
    CHECK(left->scratch_used == right->scratch_used);
}

static void test_descriptor_identity(void)
{
    rune_operation_descriptor a;
    rune_operation_descriptor b;
    uint64_t a_id;
    uint64_t b_id;

    CHECK_STATUS(
        rune_operation_make_fill(
            &a,
            span_ref(1u, 2u, 4u),
            (uint8_t)0xa5u
        ),
        RUNE_OK
    );
    b = a;

    CHECK_STATUS(
        rune_operation_descriptor_identity(&a, &a_id),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_operation_descriptor_identity(&b, &b_id),
        RUNE_OK
    );
    CHECK(a_id == b_id);

    b.parameter_u64 = UINT64_C(0xa6);
    CHECK_STATUS(
        rune_operation_descriptor_identity(&b, &b_id),
        RUNE_OK
    );
    CHECK(a_id != b_id);
}

static void test_move_replay_is_address_independent(void)
{
    uint8_t in_a[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t out_a[8] = { 0u };
    uint8_t in_b[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t out_b[8] = { 0u };
    rune_region regions_a[2];
    rune_region regions_b[2];
    rune_operation_bindings bindings_a;
    rune_operation_bindings bindings_b;
    rune_operation_descriptor descriptor;
    rune_operation_receipt receipt_a;
    rune_operation_receipt receipt_b;
    const uint8_t expected[8] = {
        0u, 0u, 2u, 3u, 4u, 5u, 0u, 0u
    };

    attach_region(&regions_a[0], in_a, 8u);
    attach_region(&regions_a[1], out_a, 8u);
    attach_region(&regions_b[0], in_b, 8u);
    attach_region(&regions_b[1], out_b, 8u);

    bindings_a.regions = regions_a;
    bindings_a.region_count = 2u;
    bindings_b.regions = regions_b;
    bindings_b.region_count = 2u;

    CHECK_STATUS(
        rune_operation_make_move(
            &descriptor,
            span_ref(0u, 1u, 4u),
            span_ref(1u, 2u, 4u)
        ),
        RUNE_OK
    );

    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings_a,
            &receipt_a
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings_b,
            &receipt_b
        ),
        RUNE_OK
    );

    CHECK(memcmp(out_a, expected, sizeof(expected)) == 0);
    CHECK(memcmp(out_b, expected, sizeof(expected)) == 0);
    check_receipt_equal(&receipt_a, &receipt_b);
    CHECK(receipt_a.execution_class == RUNE_OPERATION_EXECUTED);
    CHECK(receipt_a.bytes_read == 4u);
    CHECK(receipt_a.bytes_written == 4u);
    CHECK(receipt_a.scratch_required == 0u);
    CHECK(receipt_a.scratch_used == 0u);
    CHECK(receipt_a.result_identity != 0u);
}

static void test_fill_replay(void)
{
    uint8_t bytes_a[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t bytes_b[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    rune_region region_a;
    rune_region region_b;
    rune_operation_bindings bindings_a;
    rune_operation_bindings bindings_b;
    rune_operation_descriptor descriptor;
    rune_operation_receipt receipt_a;
    rune_operation_receipt receipt_b;
    const uint8_t expected[8] = {
        1u, 2u, 0x5au, 0x5au, 0x5au, 6u, 7u, 8u
    };

    attach_region(&region_a, bytes_a, 8u);
    attach_region(&region_b, bytes_b, 8u);

    bindings_a.regions = &region_a;
    bindings_a.region_count = 1u;
    bindings_b.regions = &region_b;
    bindings_b.region_count = 1u;

    CHECK_STATUS(
        rune_operation_make_fill(
            &descriptor,
            span_ref(0u, 2u, 3u),
            (uint8_t)0x5au
        ),
        RUNE_OK
    );

    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings_a,
            &receipt_a
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings_b,
            &receipt_b
        ),
        RUNE_OK
    );

    CHECK(memcmp(bytes_a, expected, sizeof(expected)) == 0);
    CHECK(memcmp(bytes_b, expected, sizeof(expected)) == 0);
    check_receipt_equal(&receipt_a, &receipt_b);
    CHECK(receipt_a.bytes_read == 0u);
    CHECK(receipt_a.bytes_written == 3u);
}

static void test_overlapping_move_uses_r1_semantics(void)
{
    uint8_t bytes[8] = { 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u };
    const uint8_t expected[8] = {
        0u, 1u, 0u, 1u, 2u, 3u, 6u, 7u
    };
    rune_region region;
    rune_operation_bindings bindings;
    rune_operation_descriptor descriptor;
    rune_operation_receipt receipt;

    attach_region(&region, bytes, 8u);
    bindings.regions = &region;
    bindings.region_count = 1u;

    CHECK_STATUS(
        rune_operation_make_move(
            &descriptor,
            span_ref(0u, 0u, 4u),
            span_ref(0u, 2u, 4u)
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings,
            &receipt
        ),
        RUNE_OK
    );

    CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
    CHECK(receipt.execution_class == RUNE_OPERATION_EXECUTED);
}

static void test_rejection_receipt_and_nonmutation(void)
{
    uint8_t bytes[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    uint8_t before[8];
    rune_region region;
    rune_operation_bindings bindings;
    rune_operation_descriptor descriptor;
    rune_operation_receipt receipt;

    memcpy(before, bytes, sizeof(bytes));
    attach_region(&region, bytes, 8u);
    bindings.regions = &region;
    bindings.region_count = 1u;

    CHECK_STATUS(
        rune_operation_make_fill(
            &descriptor,
            span_ref(0u, 7u, 2u),
            (uint8_t)0xffu
        ),
        RUNE_OK
    );

    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings,
            &receipt
        ),
        RUNE_ERR_OUT_OF_BOUNDS
    );

    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
    CHECK(receipt.status == (uint32_t)RUNE_ERR_OUT_OF_BOUNDS);
    CHECK(receipt.execution_class == RUNE_OPERATION_REJECTED);
    CHECK(receipt.result_identity == 0u);
    CHECK(receipt.bytes_read == 0u);
    CHECK(receipt.bytes_written == 0u);
}

static void test_unsupported_and_invalid_descriptors(void)
{
    uint8_t bytes[4] = { 0u };
    rune_region region;
    rune_operation_bindings bindings;
    rune_operation_descriptor descriptor;
    rune_operation_receipt receipt;

    attach_region(&region, bytes, 4u);
    bindings.regions = &region;
    bindings.region_count = 1u;

    descriptor.operation_id = UINT32_C(99);
    descriptor.semantic_version = RUNE_OPERATION_SEMANTIC_VERSION_V1;
    descriptor.input = rune_operation_span_none();
    descriptor.output = span_ref(0u, 0u, 1u);
    descriptor.parameter_u64 = 0u;
    descriptor.scratch_required = 0u;

    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings,
            &receipt
        ),
        RUNE_ERR_UNSUPPORTED_OPERATION
    );
    CHECK(
        receipt.status ==
        (uint32_t)RUNE_ERR_UNSUPPORTED_OPERATION
    );
    CHECK(receipt.execution_class == RUNE_OPERATION_REJECTED);

    descriptor.operation_id = RUNE_OPERATION_BYTES_FILL;
    descriptor.semantic_version = UINT32_C(2);
    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings,
            &receipt
        ),
        RUNE_ERR_UNSUPPORTED_OPERATION
    );

    descriptor.semantic_version = RUNE_OPERATION_SEMANTIC_VERSION_V1;
    descriptor.scratch_required = 1u;
    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings,
            &receipt
        ),
        RUNE_ERR_INVALID_ARGUMENT
    );
}

static void test_binding_failures(void)
{
    uint8_t bytes[4] = { 0u };
    rune_region region;
    rune_operation_bindings bindings;
    rune_operation_descriptor descriptor;
    rune_operation_receipt receipt;

    attach_region(&region, bytes, 4u);
    bindings.regions = &region;
    bindings.region_count = 1u;

    CHECK_STATUS(
        rune_operation_make_fill(
            &descriptor,
            span_ref(1u, 0u, 1u),
            (uint8_t)0x11u
        ),
        RUNE_OK
    );
    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            &bindings,
            &receipt
        ),
        RUNE_ERR_OUT_OF_BOUNDS
    );
    CHECK(receipt.execution_class == RUNE_OPERATION_REJECTED);

    CHECK_STATUS(
        rune_operation_execute(
            &descriptor,
            NULL,
            &receipt
        ),
        RUNE_ERR_NULL_ARGUMENT
    );
    CHECK(receipt.execution_class == RUNE_OPERATION_REJECTED);
}

int main(void)
{
    test_descriptor_identity();
    test_move_replay_is_address_independent();
    test_fill_replay();
    test_overlapping_move_uses_r1_semantics();
    test_rejection_receipt_and_nonmutation();
    test_unsupported_and_invalid_descriptors();
    test_binding_failures();

    if (failures != 0) {
        fprintf(stderr, "RUNE R6 operation tests: FAIL (%d)\n", failures);
        return 1;
    }

    puts("RUNE R6 operation tests: PASS");
    return 0;
}
