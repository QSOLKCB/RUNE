/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/arena.h"

#include <stdint.h>

static int rune_arena_alignment_valid(uint64_t alignment)
{
    return alignment != 0u &&
        (alignment & (alignment - (uint64_t)1u)) == 0u;
}

static rune_status rune_arena_validate(const rune_arena *arena)
{
    rune_span probe;
    rune_status status;

    if (arena == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_span_slice(
        &probe,
        &arena->storage,
        0u,
        0u,
        RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
    );
    if (status != RUNE_OK) {
        return status;
    }

    if (arena->generation == 0u) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (arena->cursor > arena->storage.length ||
        arena->high_water > arena->storage.length ||
        arena->cursor > arena->high_water) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (arena->cumulative_payload_bytes >
        arena->cumulative_consumed_bytes) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (arena->cumulative_consumed_bytes < arena->high_water) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    return RUNE_OK;
}

static rune_status rune_arena_aligned_start(
    uint64_t cursor,
    uint64_t alignment,
    uint64_t *start
)
{
    uint64_t mask;
    uint64_t remainder;
    uint64_t padding;

    if (start == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (!rune_arena_alignment_valid(alignment)) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    mask = alignment - (uint64_t)1u;
    remainder = cursor & mask;

    if (remainder == 0u) {
        *start = cursor;
        return RUNE_OK;
    }

    padding = alignment - remainder;
    if (cursor > UINT64_MAX - padding) {
        return RUNE_ERR_OVERFLOW;
    }

    *start = cursor + padding;
    return RUNE_OK;
}

static int rune_u64_add_fits(
    uint64_t left,
    uint64_t right
)
{
    return left <= UINT64_MAX - right;
}

rune_status rune_arena_init(
    rune_arena *out,
    const rune_span *storage
)
{
    rune_span validated;
    rune_arena candidate;
    rune_status status;

    if (out == NULL || storage == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_span_slice(
        &validated,
        storage,
        0u,
        storage->length,
        RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
    );
    if (status != RUNE_OK) {
        return status;
    }

    candidate.storage = validated;
    candidate.cursor = 0u;
    candidate.high_water = 0u;
    candidate.cumulative_payload_bytes = 0u;
    candidate.cumulative_consumed_bytes = 0u;
    candidate.allocation_count = 0u;
    candidate.generation = 1u;

    *out = candidate;
    return RUNE_OK;
}

rune_status rune_arena_alloc(
    rune_arena *arena,
    uint64_t length,
    uint64_t alignment,
    rune_span *out
)
{
    uint64_t start;
    uint64_t end;
    uint64_t consumed;
    uint64_t new_high_water;
    rune_status status;

    if (arena == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_arena_validate(arena);
    if (status != RUNE_OK) {
        return status;
    }

    status = rune_arena_aligned_start(arena->cursor, alignment, &start);
    if (status != RUNE_OK) {
        return status;
    }

    if (start > arena->storage.length) {
        return RUNE_ERR_EXHAUSTED;
    }

    if (length > arena->storage.length - start) {
        return RUNE_ERR_EXHAUSTED;
    }

    end = start + length;

    /*
     * A zero-length request returns the aligned empty view but consumes no
     * arena state and does not contribute to allocation traffic.
     */
    if (length == 0u) {
        return rune_span_slice(
            out,
            &arena->storage,
            start,
            0u,
            RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
        );
    }

    consumed = end - arena->cursor;

    if (!rune_u64_add_fits(
            arena->cumulative_payload_bytes,
            length) ||
        !rune_u64_add_fits(
            arena->cumulative_consumed_bytes,
            consumed) ||
        arena->allocation_count == UINT64_MAX) {
        return RUNE_ERR_OVERFLOW;
    }

    new_high_water = arena->high_water;
    if (end > new_high_water) {
        new_high_water = end;
    }

    status = rune_span_slice(
        out,
        &arena->storage,
        start,
        length,
        RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
    );
    if (status != RUNE_OK) {
        return status;
    }

    arena->cursor = end;
    arena->high_water = new_high_water;
    arena->cumulative_payload_bytes += length;
    arena->cumulative_consumed_bytes += consumed;
    arena->allocation_count += (uint64_t)1u;

    return RUNE_OK;
}

rune_status rune_arena_checkpoint_save(
    const rune_arena *arena,
    rune_arena_checkpoint *out
)
{
    rune_status status;

    if (arena == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_arena_validate(arena);
    if (status != RUNE_OK) {
        return status;
    }

    out->owner = arena;
    out->cursor = arena->cursor;
    out->generation = arena->generation;
    return RUNE_OK;
}

rune_status rune_arena_checkpoint_restore(
    rune_arena *arena,
    const rune_arena_checkpoint *checkpoint
)
{
    rune_status status;

    if (arena == NULL || checkpoint == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_arena_validate(arena);
    if (status != RUNE_OK) {
        return status;
    }

    if (checkpoint->owner != arena ||
        checkpoint->generation != arena->generation ||
        checkpoint->cursor > arena->cursor ||
        checkpoint->cursor > arena->storage.length) {
        return RUNE_ERR_STALE_CHECKPOINT;
    }

    arena->cursor = checkpoint->cursor;
    return RUNE_OK;
}

rune_status rune_arena_reset(rune_arena *arena)
{
    rune_status status;

    if (arena == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_arena_validate(arena);
    if (status != RUNE_OK) {
        return status;
    }

    if (arena->generation == UINT64_MAX) {
        return RUNE_ERR_OVERFLOW;
    }

    arena->cursor = 0u;
    arena->generation += (uint64_t)1u;
    return RUNE_OK;
}
