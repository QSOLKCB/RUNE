/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RUNE_RING_H
#define RUNE_RING_H

#include <stdint.h>

#include "rune/region.h"
#include "rune/status.h"

/*
 * R5 ring storage is borrowed from one caller-owned read/write R1 span.
 * RUNE owns only deterministic cursor state; it never allocates, frees, or
 * resizes the backing storage.
 */
typedef struct rune_ring {
    rune_span storage;
    uint64_t read_cursor;
    uint64_t write_cursor;
    uint64_t size;
} rune_ring;

/*
 * A ring view is at most two spans because one logical interval may wrap once
 * at the end of fixed-capacity storage. first precedes second logically.
 *
 * Views are logical observations/reservations of the current ring state. They
 * must be treated as expired after any successful produce/consume mutation.
 */
typedef struct rune_ring_views {
    rune_span first;
    rune_span second;
    uint64_t total_length;
} rune_ring_views;

rune_status rune_ring_init(
    rune_ring *out,
    const rune_span *storage
);

rune_status rune_ring_read_views(
    const rune_ring *ring,
    rune_ring_views *out
);

rune_status rune_ring_write_views(
    const rune_ring *ring,
    rune_ring_views *out
);

rune_status rune_ring_produce(
    rune_ring *ring,
    uint64_t length
);

rune_status rune_ring_consume(
    rune_ring *ring,
    uint64_t length
);

#endif
