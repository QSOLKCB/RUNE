/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RUNE_ARENA_H
#define RUNE_ARENA_H

#include <stdint.h>

#include "rune/region.h"
#include "rune/status.h"

/*
 * R2 arena storage is a read/write R1 span borrowed from the caller.
 * Allocation is monotonic within the current generation.
 *
 * Alignment is relative to the beginning of the arena storage span. RUNE does
 * not claim that arbitrary caller storage has native-object alignment merely
 * because an arena-relative offset is aligned.
 */
typedef struct rune_arena {
    rune_span storage;
    uint64_t cursor;
    uint64_t high_water;
    uint64_t cumulative_payload_bytes;
    uint64_t cumulative_consumed_bytes;
    uint64_t allocation_count;
    uint64_t generation;
} rune_arena;

/*
 * owner is process-local transient identity used only to reject checkpoints
 * from a different arena object. It is not a portable or serialized identity.
 * The arena object's address must remain stable while its checkpoints are used.
 */
typedef struct rune_arena_checkpoint {
    const rune_arena *owner;
    uint64_t cursor;
    uint64_t generation;
} rune_arena_checkpoint;

rune_status rune_arena_init(
    rune_arena *out,
    const rune_span *storage
);

rune_status rune_arena_alloc(
    rune_arena *arena,
    uint64_t length,
    uint64_t alignment,
    rune_span *out
);

rune_status rune_arena_checkpoint_save(
    const rune_arena *arena,
    rune_arena_checkpoint *out
);

rune_status rune_arena_checkpoint_restore(
    rune_arena *arena,
    const rune_arena_checkpoint *checkpoint
);

rune_status rune_arena_reset(
    rune_arena *arena
);

#endif
