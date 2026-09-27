/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/operation.h"
#include "rune/region.h"
#include "rune/status.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct rune_r6_vector {
    uint64_t vector_id;
    rune_operation_descriptor descriptor;
    uint8_t region0[8];
    uint64_t region0_length;
    uint8_t region1[8];
    uint64_t region1_length;
    uint32_t region_count;
    rune_status expected_status;
    uint32_t expected_execution_class;
    uint64_t expected_descriptor_identity;
    uint64_t expected_result_identity;
    uint64_t expected_bytes_read;
    uint64_t expected_bytes_written;
} rune_r6_vector;

static int rune_r6_write_byte(uint8_t byte)
{
    return fwrite(&byte, 1u, 1u, stdout) == 1u;
}

static int rune_r6_write_u64(uint64_t value)
{
    uint8_t digits[20];
    size_t count;
    size_t i;

    count = 0u;
    do {
        digits[count] = (uint8_t)(
            0x30u + (uint8_t)(value % UINT64_C(10))
        );
        value /= UINT64_C(10);
        count += 1u;
    } while (value != 0u);

    for (i = count; i != 0u; --i) {
        if (!rune_r6_write_byte(digits[i - 1u])) {
            return 0;
        }
    }

    return 1;
}

static int rune_r6_write_field(uint64_t value, int last)
{
    if (!rune_r6_write_u64(value)) {
        return 0;
    }

    return rune_r6_write_byte(last ? (uint8_t)0x0au : (uint8_t)0x09u);
}

static int rune_r6_emit_receipt(
    uint64_t vector_id,
    const rune_operation_receipt *receipt
)
{
    return
        rune_r6_write_field(vector_id, 0) &&
        rune_r6_write_field((uint64_t)receipt->operation_id, 0) &&
        rune_r6_write_field((uint64_t)receipt->semantic_version, 0) &&
        rune_r6_write_field((uint64_t)receipt->status, 0) &&
        rune_r6_write_field((uint64_t)receipt->execution_class, 0) &&
        rune_r6_write_field(receipt->descriptor_identity, 0) &&
        rune_r6_write_field(receipt->result_identity, 0) &&
        rune_r6_write_field(receipt->bytes_read, 0) &&
        rune_r6_write_field(receipt->bytes_written, 0) &&
        rune_r6_write_field(receipt->scratch_required, 0) &&
        rune_r6_write_field(receipt->scratch_used, 1);
}

static rune_operation_span_ref rune_r6_ref(
    uint32_t region_slot,
    uint64_t offset,
    uint64_t length
)
{
    rune_operation_span_ref ref;

    ref.region_slot = region_slot;
    ref.offset = offset;
    ref.length = length;
    return ref;
}

static void rune_r6_init_vectors(rune_r6_vector vectors[3])
{
    rune_operation_descriptor descriptor;

    memset(vectors, 0, sizeof(rune_r6_vector) * 3u);

    vectors[0].vector_id = UINT64_C(1);
    vectors[0].region0[0] = 0u;
    vectors[0].region0[1] = 1u;
    vectors[0].region0[2] = 2u;
    vectors[0].region0[3] = 3u;
    vectors[0].region0[4] = 4u;
    vectors[0].region0[5] = 5u;
    vectors[0].region0[6] = 6u;
    vectors[0].region0[7] = 7u;
    vectors[0].region0_length = 8u;
    vectors[0].region1[0] = 0x10u;
    vectors[0].region1[1] = 0x11u;
    vectors[0].region1[2] = 0x12u;
    vectors[0].region1[3] = 0x13u;
    vectors[0].region1[4] = 0x14u;
    vectors[0].region1[5] = 0x15u;
    vectors[0].region1[6] = 0x16u;
    vectors[0].region1[7] = 0x17u;
    vectors[0].region1_length = 8u;
    vectors[0].region_count = 2u;
    if (rune_operation_make_move(
            &descriptor,
            rune_r6_ref(0u, 1u, 4u),
            rune_r6_ref(1u, 2u, 4u)) != RUNE_OK) {
        return;
    }
    vectors[0].descriptor = descriptor;
    vectors[0].expected_status = RUNE_OK;
    vectors[0].expected_execution_class = RUNE_OPERATION_EXECUTED;
    vectors[0].expected_descriptor_identity =
        UINT64_C(4032546263986865687);
    vectors[0].expected_result_identity =
        UINT64_C(13725386680924731485);
    vectors[0].expected_bytes_read = 4u;
    vectors[0].expected_bytes_written = 4u;

    vectors[1].vector_id = UINT64_C(2);
    vectors[1].region0[0] = 0u;
    vectors[1].region0[1] = 1u;
    vectors[1].region0[2] = 2u;
    vectors[1].region0[3] = 3u;
    vectors[1].region0[4] = 4u;
    vectors[1].region0[5] = 5u;
    vectors[1].region0[6] = 6u;
    vectors[1].region0[7] = 7u;
    vectors[1].region0_length = 8u;
    vectors[1].region_count = 1u;
    if (rune_operation_make_fill(
            &descriptor,
            rune_r6_ref(0u, 2u, 3u),
            (uint8_t)0x5au) != RUNE_OK) {
        return;
    }
    vectors[1].descriptor = descriptor;
    vectors[1].expected_status = RUNE_OK;
    vectors[1].expected_execution_class = RUNE_OPERATION_EXECUTED;
    vectors[1].expected_descriptor_identity =
        UINT64_C(3063641660719482025);
    vectors[1].expected_result_identity =
        UINT64_C(16441241574006009789);
    vectors[1].expected_bytes_read = 0u;
    vectors[1].expected_bytes_written = 3u;

    vectors[2].vector_id = UINT64_C(3);
    vectors[2].region0[0] = 0xaau;
    vectors[2].region0[1] = 0xbbu;
    vectors[2].region0[2] = 0xccu;
    vectors[2].region0[3] = 0xddu;
    vectors[2].region0_length = 4u;
    vectors[2].region_count = 1u;
    if (rune_operation_make_fill(
            &descriptor,
            rune_r6_ref(0u, 3u, 2u),
            (uint8_t)0xffu) != RUNE_OK) {
        return;
    }
    vectors[2].descriptor = descriptor;
    vectors[2].expected_status = RUNE_ERR_OUT_OF_BOUNDS;
    vectors[2].expected_execution_class = RUNE_OPERATION_REJECTED;
    vectors[2].expected_descriptor_identity =
        UINT64_C(15416456910336191404);
    vectors[2].expected_result_identity = 0u;
    vectors[2].expected_bytes_read = 0u;
    vectors[2].expected_bytes_written = 0u;
}

static int rune_r6_check_vector_result(
    const rune_r6_vector *vector,
    rune_status status,
    const rune_operation_receipt *receipt,
    const uint8_t *region0,
    const uint8_t *region1
)
{
    static const uint8_t vector1_region0_expected[8] = {
        0x00u, 0x01u, 0x02u, 0x03u,
        0x04u, 0x05u, 0x06u, 0x07u
    };
    static const uint8_t vector1_region1_expected[8] = {
        0x10u, 0x11u, 0x01u, 0x02u,
        0x03u, 0x04u, 0x16u, 0x17u
    };
    static const uint8_t vector2_region0_expected[8] = {
        0x00u, 0x01u, 0x5au, 0x5au,
        0x5au, 0x05u, 0x06u, 0x07u
    };
    static const uint8_t vector3_region0_expected[4] = {
        0xaau, 0xbbu, 0xccu, 0xddu
    };

    if (status != vector->expected_status ||
        receipt->status != (uint32_t)vector->expected_status ||
        receipt->execution_class != vector->expected_execution_class ||
        receipt->descriptor_identity !=
            vector->expected_descriptor_identity ||
        receipt->result_identity != vector->expected_result_identity ||
        receipt->bytes_read != vector->expected_bytes_read ||
        receipt->bytes_written != vector->expected_bytes_written ||
        receipt->scratch_required != 0u ||
        receipt->scratch_used != 0u) {
        return 0;
    }

    if (vector->vector_id == UINT64_C(1)) {
        return memcmp(
            region0,
            vector1_region0_expected,
            sizeof(vector1_region0_expected)
        ) == 0 &&
        memcmp(
            region1,
            vector1_region1_expected,
            sizeof(vector1_region1_expected)
        ) == 0;
    }

    if (vector->vector_id == UINT64_C(2)) {
        return memcmp(
            region0,
            vector2_region0_expected,
            sizeof(vector2_region0_expected)
        ) == 0;
    }

    return memcmp(
        region0,
        vector3_region0_expected,
        sizeof(vector3_region0_expected)
    ) == 0;
}

int main(void)
{
    rune_r6_vector vectors[3];
    size_t i;

    rune_r6_init_vectors(vectors);

    for (i = 0u; i < 3u; ++i) {
        uint8_t region0_bytes[8];
        uint8_t region1_bytes[8];
        rune_region regions[2];
        rune_operation_bindings bindings;
        rune_operation_receipt receipt;
        rune_status status;

        memcpy(region0_bytes, vectors[i].region0, sizeof(region0_bytes));
        memcpy(region1_bytes, vectors[i].region1, sizeof(region1_bytes));

        if (rune_region_attach(
                &regions[0],
                region0_bytes,
                vectors[i].region0_length,
                RUNE_ACCESS_READ | RUNE_ACCESS_WRITE) != RUNE_OK) {
            return 1;
        }

        if (vectors[i].region_count == 2u) {
            if (rune_region_attach(
                    &regions[1],
                    region1_bytes,
                    vectors[i].region1_length,
                    RUNE_ACCESS_READ | RUNE_ACCESS_WRITE) != RUNE_OK) {
                return 1;
            }
        }

        bindings.regions = regions;
        bindings.region_count = vectors[i].region_count;

        status = rune_operation_execute(
            &vectors[i].descriptor,
            &bindings,
            &receipt
        );

        if (!rune_r6_check_vector_result(
                &vectors[i],
                status,
                &receipt,
                region0_bytes,
                region1_bytes)) {
            return 1;
        }

        if (!rune_r6_emit_receipt(vectors[i].vector_id, &receipt)) {
            return 1;
        }
    }

    if (fflush(stdout) == EOF || ferror(stdout)) {
        return 1;
    }

    return 0;
}
