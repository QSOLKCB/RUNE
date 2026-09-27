/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/ring.h"

#include <stddef.h>
#include <stdint.h>

static rune_status rune_ring_advance(
    uint64_t capacity,
    uint64_t cursor,
    uint64_t length,
    uint64_t *out
)
{
    uint64_t remaining;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (capacity == 0u) {
        if (cursor != 0u || length != 0u) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        *out = 0u;
        return RUNE_OK;
    }

    if (cursor >= capacity || length > capacity) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    remaining = capacity - cursor;
    if (length < remaining) {
        *out = cursor + length;
    } else if (length == remaining) {
        *out = 0u;
    } else {
        *out = length - remaining;
    }

    return RUNE_OK;
}

static rune_status rune_ring_validate(const rune_ring *ring)
{
    rune_span probe;
    uint64_t expected_write;
    rune_status status;

    if (ring == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_span_slice(
        &probe,
        &ring->storage,
        0u,
        ring->storage.length,
        RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
    );
    if (status != RUNE_OK) {
        return status;
    }

    if (ring->storage.length == 0u) {
        if (ring->read_cursor != 0u ||
            ring->write_cursor != 0u ||
            ring->size != 0u) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        return RUNE_OK;
    }

    if (ring->read_cursor >= ring->storage.length ||
        ring->write_cursor >= ring->storage.length ||
        ring->size > ring->storage.length) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    status = rune_ring_advance(
        ring->storage.length,
        ring->read_cursor,
        ring->size,
        &expected_write
    );
    if (status != RUNE_OK) {
        return status;
    }

    if (expected_write != ring->write_cursor) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    return RUNE_OK;
}

static rune_status rune_ring_make_views(
    const rune_ring *ring,
    uint64_t cursor,
    uint64_t total_length,
    uint32_t access,
    rune_ring_views *out
)
{
    rune_ring_views candidate;
    uint64_t first_length;
    uint64_t second_length;
    rune_status status;

    if (ring == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_ring_validate(ring);
    if (status != RUNE_OK) {
        return status;
    }

    if (total_length > ring->storage.length) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (ring->storage.length == 0u) {
        first_length = 0u;
        second_length = 0u;
        cursor = 0u;
    } else {
        if (cursor >= ring->storage.length) {
            return RUNE_ERR_INVALID_ARGUMENT;
        }

        first_length = ring->storage.length - cursor;
        if (first_length > total_length) {
            first_length = total_length;
        }
        second_length = total_length - first_length;
    }

    status = rune_span_slice(
        &candidate.first,
        &ring->storage,
        cursor,
        first_length,
        access
    );
    if (status != RUNE_OK) {
        return status;
    }

    status = rune_span_slice(
        &candidate.second,
        &ring->storage,
        0u,
        second_length,
        access
    );
    if (status != RUNE_OK) {
        return status;
    }

    candidate.total_length = total_length;
    *out = candidate;
    return RUNE_OK;
}

rune_status rune_ring_init(
    rune_ring *out,
    const rune_span *storage
)
{
    rune_ring candidate;
    rune_span validated;
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
    candidate.read_cursor = 0u;
    candidate.write_cursor = 0u;
    candidate.size = 0u;

    *out = candidate;
    return RUNE_OK;
}

rune_status rune_ring_read_views(
    const rune_ring *ring,
    rune_ring_views *out
)
{
    rune_status status;

    if (ring == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_ring_validate(ring);
    if (status != RUNE_OK) {
        return status;
    }

    return rune_ring_make_views(
        ring,
        ring->read_cursor,
        ring->size,
        RUNE_ACCESS_READ,
        out
    );
}

rune_status rune_ring_write_views(
    const rune_ring *ring,
    rune_ring_views *out
)
{
    uint64_t writable;
    rune_status status;

    if (ring == NULL || out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_ring_validate(ring);
    if (status != RUNE_OK) {
        return status;
    }

    writable = ring->storage.length - ring->size;

    return rune_ring_make_views(
        ring,
        ring->write_cursor,
        writable,
        RUNE_ACCESS_WRITE,
        out
    );
}

rune_status rune_ring_produce(
    rune_ring *ring,
    uint64_t length
)
{
    uint64_t next_write;
    rune_status status;

    if (ring == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_ring_validate(ring);
    if (status != RUNE_OK) {
        return status;
    }

    if (length > ring->storage.length - ring->size) {
        return RUNE_ERR_CAPACITY;
    }

    if (length == 0u) {
        return RUNE_OK;
    }

    status = rune_ring_advance(
        ring->storage.length,
        ring->write_cursor,
        length,
        &next_write
    );
    if (status != RUNE_OK) {
        return status;
    }

    ring->write_cursor = next_write;
    ring->size += length;
    return RUNE_OK;
}

rune_status rune_ring_consume(
    rune_ring *ring,
    uint64_t length
)
{
    uint64_t next_read;
    rune_status status;

    if (ring == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_ring_validate(ring);
    if (status != RUNE_OK) {
        return status;
    }

    if (length > ring->size) {
        return RUNE_ERR_OUT_OF_BOUNDS;
    }

    if (length == 0u) {
        return RUNE_OK;
    }

    status = rune_ring_advance(
        ring->storage.length,
        ring->read_cursor,
        length,
        &next_read
    );
    if (status != RUNE_OK) {
        return status;
    }

    ring->read_cursor = next_read;
    ring->size -= length;
    return RUNE_OK;
}
