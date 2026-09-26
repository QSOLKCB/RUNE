/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/region.h"

#include <stddef.h>
#include <string.h>

static int rune_access_valid(uint32_t access)
{
    return access != 0u && (access & ~RUNE_ACCESS_MASK) == 0u;
}

static int rune_capacity_fits_host(uint64_t capacity)
{
    size_t host_capacity;

    host_capacity = (size_t)capacity;
    return (uint64_t)host_capacity == capacity;
}

static rune_status rune_extent_end(
    uint64_t offset,
    uint64_t length,
    uint64_t *end
)
{
    if (end == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (offset > UINT64_MAX - length) {
        return RUNE_ERR_OVERFLOW;
    }

    *end = offset + length;
    return RUNE_OK;
}

static rune_status rune_region_validate(const rune_region *region)
{
    if (region == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (!rune_access_valid(region->access)) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (!rune_capacity_fits_host(region->capacity)) {
        return RUNE_ERR_UNSUPPORTED_CAPACITY;
    }

    if (region->capacity != 0u && region->data == NULL) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    return RUNE_OK;
}

static rune_status rune_span_validate(const rune_span *span)
{
    uint64_t end;
    rune_status status;

    if (span == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_region_validate(span->region);
    if (status != RUNE_OK) {
        return status;
    }

    if (!rune_access_valid(span->access)) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if ((span->access & span->region->access) != span->access) {
        return RUNE_ERR_ACCESS;
    }

    status = rune_extent_end(span->offset, span->length, &end);
    if (status != RUNE_OK) {
        return status;
    }

    if (end > span->region->capacity) {
        return RUNE_ERR_OUT_OF_BOUNDS;
    }

    return RUNE_OK;
}

rune_status rune_region_attach(
    rune_region *out,
    void *storage,
    uint64_t capacity,
    uint32_t access
)
{
    rune_region candidate;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (!rune_access_valid(access)) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (!rune_capacity_fits_host(capacity)) {
        return RUNE_ERR_UNSUPPORTED_CAPACITY;
    }

    if (capacity != 0u && storage == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    candidate.data = (uint8_t *)storage;
    candidate.capacity = capacity;
    candidate.access = access;

    *out = candidate;
    return RUNE_OK;
}

rune_status rune_span_init(
    rune_span *out,
    rune_region *region,
    uint64_t offset,
    uint64_t length,
    uint32_t access
)
{
    rune_span candidate;
    rune_status status;

    if (out == NULL || region == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    candidate.region = region;
    candidate.offset = offset;
    candidate.length = length;
    candidate.access = access;

    status = rune_span_validate(&candidate);
    if (status != RUNE_OK) {
        return status;
    }

    *out = candidate;
    return RUNE_OK;
}

rune_status rune_span_slice(
    rune_span *out,
    const rune_span *parent,
    uint64_t relative_offset,
    uint64_t length,
    uint32_t access
)
{
    uint64_t relative_end;
    uint64_t absolute_offset;
    rune_status status;

    if (out == NULL || parent == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_span_validate(parent);
    if (status != RUNE_OK) {
        return status;
    }

    if (!rune_access_valid(access)) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if ((access & parent->access) != access) {
        return RUNE_ERR_ACCESS;
    }

    status = rune_extent_end(relative_offset, length, &relative_end);
    if (status != RUNE_OK) {
        return status;
    }

    if (relative_end > parent->length) {
        return RUNE_ERR_OUT_OF_BOUNDS;
    }

    status = rune_extent_end(parent->offset, relative_offset, &absolute_offset);
    if (status != RUNE_OK) {
        return status;
    }

    return rune_span_init(
        out,
        parent->region,
        absolute_offset,
        length,
        access
    );
}

rune_status rune_span_move(
    rune_span *dst,
    const rune_span *src
)
{
    rune_status status;
    uint8_t *dst_ptr;
    const uint8_t *src_ptr;

    status = rune_span_validate(src);
    if (status != RUNE_OK) {
        return status;
    }

    status = rune_span_validate(dst);
    if (status != RUNE_OK) {
        return status;
    }

    if ((src->access & RUNE_ACCESS_READ) == 0u) {
        return RUNE_ERR_ACCESS;
    }

    if ((dst->access & RUNE_ACCESS_WRITE) == 0u) {
        return RUNE_ERR_ACCESS;
    }

    if (src->length > dst->length) {
        return RUNE_ERR_CAPACITY;
    }

    if (src->length == 0u) {
        return RUNE_OK;
    }

    dst_ptr = dst->region->data + (size_t)dst->offset;
    src_ptr = src->region->data + (size_t)src->offset;

    memmove(dst_ptr, src_ptr, (size_t)src->length);
    return RUNE_OK;
}

rune_status rune_span_fill(
    rune_span *dst,
    uint8_t value
)
{
    rune_status status;
    uint8_t *dst_ptr;

    status = rune_span_validate(dst);
    if (status != RUNE_OK) {
        return status;
    }

    if ((dst->access & RUNE_ACCESS_WRITE) == 0u) {
        return RUNE_ERR_ACCESS;
    }

    if (dst->length == 0u) {
        return RUNE_OK;
    }

    dst_ptr = dst->region->data + (size_t)dst->offset;
    memset(dst_ptr, (int)value, (size_t)dst->length);
    return RUNE_OK;
}

rune_status rune_span_clear(rune_span *dst)
{
    return rune_span_fill(dst, (uint8_t)0u);
}
