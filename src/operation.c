/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/operation.h"

#include <stddef.h>
#include <stdint.h>

static uint64_t rune_operation_hash_u32(
    uint64_t hash,
    uint32_t value
)
{
    uint32_t lane;

    for (lane = 0u; lane < 4u; ++lane) {
        hash ^= (uint64_t)(value & UINT32_C(0xff));
        hash *= UINT64_C(1099511628211);
        value >>= 8u;
    }

    return hash;
}

static uint64_t rune_operation_hash_u64(
    uint64_t hash,
    uint64_t value
)
{
    uint32_t lane;

    for (lane = 0u; lane < 8u; ++lane) {
        hash ^= value & UINT64_C(0xff);
        hash *= UINT64_C(1099511628211);
        value >>= 8u;
    }

    return hash;
}

static uint64_t rune_operation_hash_readable_span(
    const rune_span *span
)
{
    uint64_t hash;
    uint64_t i;

    hash = UINT64_C(14695981039346656037);
    for (i = 0u; i < span->length; ++i) {
        hash ^= (uint64_t)span->region->data[
            (size_t)(span->offset + i)
        ];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static uint64_t rune_operation_hash_repeated_byte(
    uint8_t value,
    uint64_t length
)
{
    uint64_t hash;
    uint64_t i;

    hash = UINT64_C(14695981039346656037);
    for (i = 0u; i < length; ++i) {
        hash ^= (uint64_t)value;
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

/*
 * Portable alias preflight without relational comparison of unrelated
 * pointers. For two non-empty contiguous ranges, overlap implies that at
 * least one range start occurs within the other range.
 */
static int rune_operation_object_overlaps_span(
    const void *object,
    size_t object_size,
    const rune_span *span
)
{
    const uint8_t *object_start;
    const uint8_t *span_start;
    size_t span_length;
    size_t i;

    if (object == NULL || span == NULL ||
        object_size == 0u || span->length == 0u) {
        return 0;
    }

    object_start = (const uint8_t *)object;
    span_start = span->region->data + (size_t)span->offset;
    span_length = (size_t)span->length;

    for (i = 0u; i < span_length; ++i) {
        if ((const void *)object_start ==
            (const void *)(span_start + i)) {
            return 1;
        }
    }

    for (i = 0u; i < object_size; ++i) {
        if ((const void *)span_start ==
            (const void *)(object_start + i)) {
            return 1;
        }
    }

    return 0;
}

static int rune_operation_span_ref_is_none(
    rune_operation_span_ref ref
)
{
    return ref.region_slot == RUNE_OPERATION_REGION_NONE &&
        ref.offset == 0u &&
        ref.length == 0u;
}

static int rune_operation_span_ref_is_required(
    rune_operation_span_ref ref
)
{
    return ref.region_slot != RUNE_OPERATION_REGION_NONE;
}

static rune_status rune_operation_resolve_span(
    const rune_operation_bindings *bindings,
    rune_operation_span_ref ref,
    uint32_t access,
    rune_span *out
)
{
    if (bindings == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (bindings->regions == NULL && bindings->region_count != 0u) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (ref.region_slot == RUNE_OPERATION_REGION_NONE ||
        ref.region_slot >= bindings->region_count) {
        return RUNE_ERR_OUT_OF_BOUNDS;
    }

    return rune_span_init(
        out,
        &bindings->regions[ref.region_slot],
        ref.offset,
        ref.length,
        access
    );
}

static rune_status rune_operation_validate_descriptor(
    const rune_operation_descriptor *descriptor
)
{
    if (descriptor == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (descriptor->semantic_version !=
        RUNE_OPERATION_SEMANTIC_VERSION_V1) {
        return RUNE_ERR_UNSUPPORTED_OPERATION;
    }

    if (descriptor->operation_id != RUNE_OPERATION_BYTES_MOVE &&
        descriptor->operation_id != RUNE_OPERATION_BYTES_FILL) {
        return RUNE_ERR_UNSUPPORTED_OPERATION;
    }

    if (descriptor->scratch_required != 0u) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (descriptor->operation_id == RUNE_OPERATION_BYTES_MOVE) {
        if (!rune_operation_span_ref_is_required(descriptor->input) ||
            !rune_operation_span_ref_is_required(descriptor->output) ||
            descriptor->input.length != descriptor->output.length ||
            descriptor->parameter_u64 != 0u) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        return RUNE_OK;
    }

    if (descriptor->operation_id == RUNE_OPERATION_BYTES_FILL) {
        if (!rune_operation_span_ref_is_none(descriptor->input) ||
            !rune_operation_span_ref_is_required(descriptor->output) ||
            descriptor->parameter_u64 > UINT64_C(255)) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        return RUNE_OK;
    }

    return RUNE_ERR_UNSUPPORTED_OPERATION;
}

static void rune_operation_receipt_begin(
    rune_operation_receipt *receipt,
    const rune_operation_descriptor *descriptor,
    uint64_t descriptor_identity
)
{
    receipt->operation_id = descriptor->operation_id;
    receipt->semantic_version = descriptor->semantic_version;
    receipt->status = (uint32_t)RUNE_OK;
    receipt->execution_class = RUNE_OPERATION_REJECTED;
    receipt->descriptor_identity = descriptor_identity;
    receipt->result_identity = 0u;
    receipt->bytes_read = 0u;
    receipt->bytes_written = 0u;
    receipt->scratch_required = descriptor->scratch_required;
    receipt->scratch_used = 0u;
}

static rune_status rune_operation_reject(
    rune_operation_receipt *receipt,
    rune_status status
)
{
    receipt->status = (uint32_t)status;
    receipt->execution_class = RUNE_OPERATION_REJECTED;
    return status;
}

rune_operation_span_ref rune_operation_span_none(void)
{
    rune_operation_span_ref ref;

    ref.region_slot = RUNE_OPERATION_REGION_NONE;
    ref.offset = 0u;
    ref.length = 0u;
    return ref;
}

rune_status rune_operation_make_move(
    rune_operation_descriptor *out,
    rune_operation_span_ref input,
    rune_operation_span_ref output
)
{
    rune_operation_descriptor candidate;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    candidate.operation_id = RUNE_OPERATION_BYTES_MOVE;
    candidate.semantic_version = RUNE_OPERATION_SEMANTIC_VERSION_V1;
    candidate.input = input;
    candidate.output = output;
    candidate.parameter_u64 = 0u;
    candidate.scratch_required = 0u;

    status = rune_operation_validate_descriptor(&candidate);
    if (status != RUNE_OK) {
        return status;
    }

    *out = candidate;
    return RUNE_OK;
}

rune_status rune_operation_make_fill(
    rune_operation_descriptor *out,
    rune_operation_span_ref output,
    uint8_t value
)
{
    rune_operation_descriptor candidate;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    candidate.operation_id = RUNE_OPERATION_BYTES_FILL;
    candidate.semantic_version = RUNE_OPERATION_SEMANTIC_VERSION_V1;
    candidate.input = rune_operation_span_none();
    candidate.output = output;
    candidate.parameter_u64 = (uint64_t)value;
    candidate.scratch_required = 0u;

    status = rune_operation_validate_descriptor(&candidate);
    if (status != RUNE_OK) {
        return status;
    }

    *out = candidate;
    return RUNE_OK;
}

rune_status rune_operation_descriptor_identity(
    const rune_operation_descriptor *descriptor,
    uint64_t *out
)
{
    uint64_t hash;

    if (descriptor == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    hash = UINT64_C(14695981039346656037);
    hash = rune_operation_hash_u32(hash, descriptor->operation_id);
    hash = rune_operation_hash_u32(hash, descriptor->semantic_version);
    hash = rune_operation_hash_u32(hash, descriptor->input.region_slot);
    hash = rune_operation_hash_u64(hash, descriptor->input.offset);
    hash = rune_operation_hash_u64(hash, descriptor->input.length);
    hash = rune_operation_hash_u32(hash, descriptor->output.region_slot);
    hash = rune_operation_hash_u64(hash, descriptor->output.offset);
    hash = rune_operation_hash_u64(hash, descriptor->output.length);
    hash = rune_operation_hash_u64(hash, descriptor->parameter_u64);
    hash = rune_operation_hash_u64(hash, descriptor->scratch_required);

    *out = hash;
    return RUNE_OK;
}

rune_status rune_operation_execute(
    const rune_operation_descriptor *descriptor,
    const rune_operation_bindings *bindings,
    rune_operation_receipt *receipt
)
{
    rune_operation_descriptor operation;
    rune_operation_receipt candidate;
    rune_span input;
    rune_span output;
    uint64_t descriptor_identity;
    uint64_t result_identity;
    rune_status status;

    if (descriptor == NULL || receipt == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    /*
     * Work from a local semantic snapshot so writing the completed receipt
     * cannot change fields still needed by execution.
     */
    operation = *descriptor;

    status = rune_operation_descriptor_identity(
        &operation,
        &descriptor_identity
    );
    if (status != RUNE_OK) {
        return status;
    }

    rune_operation_receipt_begin(
        &candidate,
        &operation,
        descriptor_identity
    );

    status = rune_operation_validate_descriptor(&operation);
    if (status != RUNE_OK) {
        rune_operation_reject(&candidate, status);
        *receipt = candidate;
        return status;
    }

    if (bindings == NULL) {
        status = RUNE_ERR_NULL_ARGUMENT;
        rune_operation_reject(&candidate, status);
        *receipt = candidate;
        return status;
    }

    if (operation.operation_id == RUNE_OPERATION_BYTES_MOVE) {
        status = rune_operation_resolve_span(
            bindings,
            operation.input,
            RUNE_ACCESS_READ,
            &input
        );
        if (status != RUNE_OK) {
            rune_operation_reject(&candidate, status);
            *receipt = candidate;
            return status;
        }

        if (rune_operation_object_overlaps_span(
                receipt,
                sizeof(*receipt),
                &input)) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        status = rune_operation_resolve_span(
            bindings,
            operation.output,
            RUNE_ACCESS_WRITE,
            &output
        );
        if (status != RUNE_OK) {
            rune_operation_reject(&candidate, status);
            *receipt = candidate;
            return status;
        }

        if (rune_operation_object_overlaps_span(
                receipt,
                sizeof(*receipt),
                &output)) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        /*
         * memmove makes the final output bytes equal to the pre-move input
         * bytes even when source and destination overlap. Hash the readable
         * source before mutation so write-only output storage is never read.
         */
        result_identity = rune_operation_hash_readable_span(&input);

        status = rune_span_move(&output, &input);
        if (status != RUNE_OK) {
            rune_operation_reject(&candidate, status);
            *receipt = candidate;
            return status;
        }

        candidate.bytes_read = input.length;
        candidate.bytes_written = output.length;
    } else {
        status = rune_operation_resolve_span(
            bindings,
            operation.output,
            RUNE_ACCESS_WRITE,
            &output
        );
        if (status != RUNE_OK) {
            rune_operation_reject(&candidate, status);
            *receipt = candidate;
            return status;
        }

        if (rune_operation_object_overlaps_span(
                receipt,
                sizeof(*receipt),
                &output)) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        result_identity = rune_operation_hash_repeated_byte(
            (uint8_t)operation.parameter_u64,
            output.length
        );

        status = rune_span_fill(
            &output,
            (uint8_t)operation.parameter_u64
        );
        if (status != RUNE_OK) {
            rune_operation_reject(&candidate, status);
            *receipt = candidate;
            return status;
        }

        candidate.bytes_read = 0u;
        candidate.bytes_written = output.length;
    }

    candidate.result_identity = result_identity;
    candidate.status = (uint32_t)RUNE_OK;
    candidate.execution_class = RUNE_OPERATION_EXECUTED;
    *receipt = candidate;
    return RUNE_OK;
}
