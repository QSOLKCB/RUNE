/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RUNE_OPERATION_H
#define RUNE_OPERATION_H

#include <stdint.h>

#include "rune/region.h"
#include "rune/status.h"

#define RUNE_OPERATION_BYTES_MOVE UINT32_C(1)
#define RUNE_OPERATION_BYTES_FILL UINT32_C(2)
#define RUNE_OPERATION_SEMANTIC_VERSION_V1 UINT32_C(1)

#define RUNE_OPERATION_REGION_NONE UINT32_MAX

#define RUNE_OPERATION_REJECTED UINT32_C(0)
#define RUNE_OPERATION_EXECUTED UINT32_C(1)

typedef struct rune_operation_span_ref {
    uint32_t region_slot;
    uint64_t offset;
    uint64_t length;
} rune_operation_span_ref;

/*
 * R6 intentionally supports one input, one output, one versioned integer
 * parameter, and an explicit scratch requirement. The two demonstrated v1
 * operations use this shape:
 *
 * BYTES_MOVE:
 *   input + output required, equal lengths, parameter_u64 == 0,
 *   scratch_required == 0.
 *
 * BYTES_FILL:
 *   input must be canonical NONE, output required,
 *   parameter_u64 is the fill byte 0..255,
 *   scratch_required == 0.
 */
typedef struct rune_operation_descriptor {
    uint32_t operation_id;
    uint32_t semantic_version;
    rune_operation_span_ref input;
    rune_operation_span_ref output;
    uint64_t parameter_u64;
    uint64_t scratch_required;
} rune_operation_descriptor;

/*
 * Region slots are portable descriptor identity. The binding table is local
 * execution context mapping those slots to caller-owned region objects.
 * Native pointers in the bindings are not part of descriptor identity.
 */
typedef struct rune_operation_bindings {
    rune_region *regions;
    uint32_t region_count;
} rune_operation_bindings;

typedef struct rune_operation_receipt {
    uint32_t operation_id;
    uint32_t semantic_version;
    uint32_t status;
    uint32_t execution_class;
    uint64_t descriptor_identity;
    uint64_t result_identity;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t scratch_required;
    uint64_t scratch_used;
} rune_operation_receipt;

rune_operation_span_ref rune_operation_span_none(void);

rune_status rune_operation_make_move(
    rune_operation_descriptor *out,
    rune_operation_span_ref input,
    rune_operation_span_ref output
);

rune_status rune_operation_make_fill(
    rune_operation_descriptor *out,
    rune_operation_span_ref output,
    uint8_t value
);

rune_status rune_operation_descriptor_identity(
    const rune_operation_descriptor *descriptor,
    uint64_t *out
);

rune_status rune_operation_execute(
    const rune_operation_descriptor *descriptor,
    const rune_operation_bindings *bindings,
    rune_operation_receipt *receipt
);

#endif
