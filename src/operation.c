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

static uint64_t rune_operation_hash_bytes(
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
    rune_span input;
    rune_span output;
    uint64_t descriptor_identity;
    rune_status status;

    if (descriptor == NULL || receipt == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_operation_descriptor_identity(
        descriptor,
        &descriptor_identity
    );
    if (status != RUNE_OK) {
        return status;
    }

    rune_operation_receipt_begin(
        receipt,
        descriptor,
        descriptor_identity
    );

    status = rune_operation_validate_descriptor(descriptor);
    if (status != RUNE_OK) {
        return rune_operation_reject(receipt, status);
    }

    if (bindings == NULL) {
        return rune_operation_reject(
            receipt,
            RUNE_ERR_NULL_ARGUMENT
        );
    }

    if (descriptor->operation_id == RUNE_OPERATION_BYTES_MOVE) {
        status = rune_operation_resolve_span(
            bindings,
            descriptor->input,
            RUNE_ACCESS_READ,
            &input
        );
        if (status != RUNE_OK) {
            return rune_operation_reject(receipt, status);
        }

        status = rune_operation_resolve_span(
            bindings,
            descriptor->output,
            RUNE_ACCESS_WRITE,
            &output
        );
        if (status != RUNE_OK) {
            return rune_operation_reject(receipt, status);
        }

        status = rune_span_move(&output, &input);
        if (status != RUNE_OK) {
            return rune_operation_reject(receipt, status);
        }

        receipt->bytes_read = input.length;
        receipt->bytes_written = output.length;
    } else {
        status = rune_operation_resolve_span(
            bindings,
            descriptor->output,
            RUNE_ACCESS_WRITE,
            &output
        );
        if (status != RUNE_OK) {
            return rune_operation_reject(receipt, status);
        }

        status = rune_span_fill(
            &output,
            (uint8_t)descriptor->parameter_u64
        );
        if (status != RUNE_OK) {
            return rune_operation_reject(receipt, status);
        }

        receipt->bytes_read = 0u;
        receipt->bytes_written = output.length;
    }

    receipt->result_identity = rune_operation_hash_bytes(&output);
    receipt->status = (uint32_t)RUNE_OK;
    receipt->execution_class = RUNE_OPERATION_EXECUTED;
    return RUNE_OK;
}
