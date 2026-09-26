/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RUNE_REGION_H
#define RUNE_REGION_H

#include <stdint.h>

#include "rune/status.h"

#define RUNE_ACCESS_READ  ((uint32_t)1u)
#define RUNE_ACCESS_WRITE ((uint32_t)2u)
#define RUNE_ACCESS_MASK  (RUNE_ACCESS_READ | RUNE_ACCESS_WRITE)

/*
 * R1 regions borrow caller-owned storage. The caller retains ownership and must
 * keep the backing object alive for every operation using the region or spans
 * derived from it.
 *
 * capacity, offset, and length are portable semantic quantities. Native pointer
 * arithmetic is performed only after the implementation proves those values fit
 * the host size_t range.
 */
typedef struct rune_region {
    uint8_t *data;
    uint64_t capacity;
    uint32_t access;
} rune_region;

typedef struct rune_span {
    rune_region *region;
    uint64_t offset;
    uint64_t length;
    uint32_t access;
} rune_span;

rune_status rune_region_attach(
    rune_region *out,
    void *storage,
    uint64_t capacity,
    uint32_t access
);

rune_status rune_span_init(
    rune_span *out,
    rune_region *region,
    uint64_t offset,
    uint64_t length,
    uint32_t access
);

rune_status rune_span_slice(
    rune_span *out,
    const rune_span *parent,
    uint64_t relative_offset,
    uint64_t length,
    uint32_t access
);

/*
 * Move exactly src->length bytes into dst. dst may be larger than src.
 * Overlap is supported. RUNE_ERR_CAPACITY is returned if dst is too short.
 */
rune_status rune_span_move(
    rune_span *dst,
    const rune_span *src
);

rune_status rune_span_fill(
    rune_span *dst,
    uint8_t value
);

rune_status rune_span_clear(
    rune_span *dst
);

#endif
