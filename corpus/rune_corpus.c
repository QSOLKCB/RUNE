/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/arena.h"
#include "rune/numeric.h"
#include "rune/region.h"
#include "rune/status.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNE_CORPUS_DEFAULT_SEED UINT64_C(303)
#define RUNE_CORPUS_SMOKE_BYTES UINT64_C(32768)
#define RUNE_CORPUS_MAX_BYTES UINT64_C(268435456)
#define RUNE_CORPUS_MAX_RECEIPTS 24u

typedef struct rune_corpus_receipt {
    const char *workload;
    uint64_t seed;
    uint64_t logical_items;
    uint64_t resident_bytes;
    uint64_t scratch_bytes;
    uint64_t model_bytes_read;
    uint64_t model_bytes_written;
    uint64_t result_u64;
    uint32_t variants_checked;
} rune_corpus_receipt;

typedef struct rune_corpus_record {
    uint32_t x;
    uint32_t y;
    uint32_t flags;
    uint32_t cold0;
    uint32_t cold1;
    uint32_t cold2;
} rune_corpus_record;

typedef struct rune_corpus_node {
    uint32_t next;
    uint32_t payload;
} rune_corpus_node;

typedef struct rune_corpus_counts {
    uint64_t lt;
    uint64_t gt;
    uint64_t whitespace;
    uint64_t digit;
    uint64_t high;
    uint64_t other;
} rune_corpus_counts;

static uint64_t rune_corpus_hash_u64(uint64_t hash, uint64_t value)
{
    uint32_t lane;

    for (lane = 0u; lane < 8u; ++lane) {
        hash ^= value & UINT64_C(0xff);
        hash *= UINT64_C(1099511628211);
        value >>= 8u;
    }

    return hash;
}

static uint64_t rune_corpus_hash_text(uint64_t hash, const char *text)
{
    const unsigned char *cursor;

    cursor = (const unsigned char *)text;
    while (*cursor != 0u) {
        hash ^= (uint64_t)*cursor;
        hash *= UINT64_C(1099511628211);
        ++cursor;
    }

    return hash;
}

static uint32_t rune_corpus_value32(uint64_t seed, uint64_t index)
{
    uint64_t z;

    z = index + seed + UINT64_C(0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30u)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27u)) * UINT64_C(0x94d049bb133111eb);
    z ^= z >> 31u;
    return (uint32_t)(z >> 32u);
}

static uint32_t rune_corpus_transform32(uint32_t value)
{
    return value * UINT32_C(1664525) + UINT32_C(1013904223);
}

static int rune_corpus_is_power_of_two_u64(uint64_t value)
{
    return value != 0u && (value & (value - UINT64_C(1))) == 0u;
}

static void *rune_corpus_malloc(uint64_t bytes)
{
    size_t host_bytes;

    host_bytes = (size_t)bytes;
    if ((uint64_t)host_bytes != bytes) {
        return NULL;
    }

    return malloc(host_bytes);
}

static int rune_corpus_add_receipt(
    rune_corpus_receipt *receipts,
    size_t *count,
    const char *workload,
    uint64_t seed,
    uint64_t logical_items,
    uint64_t resident_bytes,
    uint64_t scratch_bytes,
    uint64_t model_bytes_read,
    uint64_t model_bytes_written,
    uint64_t result_u64,
    uint32_t variants_checked
)
{
    rune_corpus_receipt *receipt;

    if (*count >= (size_t)RUNE_CORPUS_MAX_RECEIPTS) {
        return 0;
    }

    receipt = &receipts[*count];
    receipt->workload = workload;
    receipt->seed = seed;
    receipt->logical_items = logical_items;
    receipt->resident_bytes = resident_bytes;
    receipt->scratch_bytes = scratch_bytes;
    receipt->model_bytes_read = model_bytes_read;
    receipt->model_bytes_written = model_bytes_written;
    receipt->result_u64 = result_u64;
    receipt->variants_checked = variants_checked;
    *count += 1u;
    return 1;
}

static uint64_t rune_corpus_receipt_fingerprint(
    const rune_corpus_receipt *receipts,
    size_t count
)
{
    uint64_t hash;
    size_t i;

    hash = UINT64_C(1469598103934665603);
    for (i = 0u; i < count; ++i) {
        hash = rune_corpus_hash_text(hash, receipts[i].workload);
        hash = rune_corpus_hash_u64(hash, receipts[i].seed);
        hash = rune_corpus_hash_u64(hash, receipts[i].logical_items);
        hash = rune_corpus_hash_u64(hash, receipts[i].resident_bytes);
        hash = rune_corpus_hash_u64(hash, receipts[i].scratch_bytes);
        hash = rune_corpus_hash_u64(hash, receipts[i].model_bytes_read);
        hash = rune_corpus_hash_u64(hash, receipts[i].model_bytes_written);
        hash = rune_corpus_hash_u64(hash, receipts[i].result_u64);
        hash = rune_corpus_hash_u64(
            hash,
            (uint64_t)receipts[i].variants_checked
        );
    }

    return hash;
}

static void rune_corpus_emit_receipt(
    const char *profile,
    uint64_t working_set_bytes,
    const rune_corpus_receipt *receipt
)
{
    printf(
        "{\"contract\":\"rune.corpus.receipt.v1\","
        "\"profile\":\"%s\","
        "\"working_set_bytes\":%" PRIu64 ","
        "\"workload\":\"%s\","
        "\"seed\":%" PRIu64 ","
        "\"logical_items\":%" PRIu64 ","
        "\"resident_bytes\":%" PRIu64 ","
        "\"scratch_bytes\":%" PRIu64 ","
        "\"model_bytes_read\":%" PRIu64 ","
        "\"model_bytes_written\":%" PRIu64 ","
        "\"result_u64\":%" PRIu64 ","
        "\"variants_checked\":%" PRIu32 "}\n",
        profile,
        working_set_bytes,
        receipt->workload,
        receipt->seed,
        receipt->logical_items,
        receipt->resident_bytes,
        receipt->scratch_bytes,
        receipt->model_bytes_read,
        receipt->model_bytes_written,
        receipt->result_u64,
        receipt->variants_checked
    );
}

static void rune_corpus_emit_summary(
    const char *profile,
    uint64_t working_set_bytes,
    uint64_t seed,
    const rune_corpus_receipt *receipts,
    size_t receipt_count
)
{
    uint64_t fingerprint;

    fingerprint = rune_corpus_receipt_fingerprint(receipts, receipt_count);
    printf(
        "{\"contract\":\"rune.corpus.summary.v1\","
        "\"profile\":\"%s\","
        "\"working_set_bytes\":%" PRIu64 ","
        "\"seed\":%" PRIu64 ","
        "\"receipt_count\":%lu,"
        "\"fingerprint_u64\":%" PRIu64 "}\n",
        profile,
        working_set_bytes,
        seed,
        (unsigned long)receipt_count,
        fingerprint
    );
}

static int rune_corpus_build_values(
    uint64_t seed,
    uint64_t count,
    uint32_t **out
)
{
    uint64_t bytes;
    uint32_t *values;
    uint64_t i;

    if (count > UINT64_MAX / (uint64_t)sizeof(uint32_t)) {
        return 0;
    }

    bytes = count * (uint64_t)sizeof(uint32_t);
    values = (uint32_t *)rune_corpus_malloc(bytes);
    if (values == NULL) {
        return 0;
    }

    for (i = 0u; i < count; ++i) {
        values[(size_t)i] = rune_corpus_value32(seed, i);
    }

    *out = values;
    return 1;
}

static uint64_t rune_corpus_sum_u32(
    const uint32_t *values,
    uint64_t count
)
{
    uint64_t result;
    uint64_t i;

    result = 0u;
    for (i = 0u; i < count; ++i) {
        result += (uint64_t)values[(size_t)i];
    }
    return result;
}

static int rune_corpus_run_c01_to_c06(
    uint64_t seed,
    uint64_t count,
    uint32_t *values,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    uint64_t bytes;
    uint64_t sequential;
    uint64_t strided;
    uint64_t permuted;
    uint64_t chased;
    uint64_t materialized;
    uint64_t fused;
    uint64_t i;
    uint64_t index;
    uint64_t mask;
    uint64_t stride;
    uint64_t multiplier;
    uint64_t increment;
    uint32_t *temporary;
    rune_corpus_node *nodes;

    bytes = count * (uint64_t)sizeof(uint32_t);
    mask = count - UINT64_C(1);

    sequential = rune_corpus_sum_u32(values, count);

    stride = ((seed << 1u) | UINT64_C(1)) & mask;
    if (stride == 0u) {
        stride = UINT64_C(1);
    }

    strided = 0u;
    index = 0u;
    for (i = 0u; i < count; ++i) {
        strided += (uint64_t)values[(size_t)index];
        index = (index + stride) & mask;
    }

    multiplier = ((seed * UINT64_C(2)) + UINT64_C(1)) & mask;
    if (multiplier == 0u) {
        multiplier = UINT64_C(1);
    }
    increment = (seed ^ UINT64_C(0xa5a5a5a5)) & mask;

    permuted = 0u;
    for (i = 0u; i < count; ++i) {
        index = (multiplier * i + increment) & mask;
        permuted += (uint64_t)values[(size_t)index];
    }

    if (sequential != strided || sequential != permuted) {
        return 0;
    }

    nodes = (rune_corpus_node *)rune_corpus_malloc(
        count * (uint64_t)sizeof(rune_corpus_node)
    );
    if (nodes == NULL) {
        return 0;
    }

    for (i = 0u; i < count; ++i) {
        nodes[(size_t)i].next = (uint32_t)((i + stride) & mask);
        nodes[(size_t)i].payload = values[(size_t)i];
    }

    chased = 0u;
    index = 0u;
    for (i = 0u; i < count; ++i) {
        chased += (uint64_t)nodes[(size_t)index].payload;
        index = (uint64_t)nodes[(size_t)index].next;
    }

    if (chased != sequential) {
        free(nodes);
        return 0;
    }

    temporary = (uint32_t *)rune_corpus_malloc(bytes);
    if (temporary == NULL) {
        free(nodes);
        return 0;
    }

    for (i = 0u; i < count; ++i) {
        temporary[(size_t)i] =
            rune_corpus_transform32(values[(size_t)i]);
    }

    materialized = rune_corpus_sum_u32(temporary, count);
    fused = 0u;
    for (i = 0u; i < count; ++i) {
        fused += (uint64_t)rune_corpus_transform32(
            values[(size_t)i]
        );
    }

    if (materialized != fused) {
        free(temporary);
        free(nodes);
        return 0;
    }

    if (!rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C01-sequential-scan",
            seed,
            count,
            bytes,
            0u,
            bytes,
            0u,
            sequential,
            1u) ||
        !rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C02-strided-scan",
            seed,
            count,
            bytes,
            0u,
            bytes,
            0u,
            strided,
            1u) ||
        !rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C03-deterministic-permutation",
            seed,
            count,
            bytes,
            0u,
            bytes,
            0u,
            permuted,
            1u) ||
        !rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C04-offset-chase",
            seed,
            count,
            count * (uint64_t)sizeof(rune_corpus_node),
            0u,
            count * (uint64_t)sizeof(rune_corpus_node),
            0u,
            chased,
            1u) ||
        !rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C05-materialize-reduce",
            seed,
            count,
            bytes * UINT64_C(2),
            bytes,
            bytes * UINT64_C(2),
            bytes,
            materialized,
            1u) ||
        !rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C06-fused-reduce",
            seed,
            count,
            bytes,
            0u,
            bytes,
            0u,
            fused,
            1u)) {
        free(temporary);
        free(nodes);
        return 0;
    }

    free(temporary);
    free(nodes);
    return 1;
}

static uint64_t rune_corpus_hot_result(
    uint32_t x,
    uint32_t y,
    uint32_t flags
)
{
    return (uint64_t)x * UINT64_C(3) +
        (uint64_t)y * UINT64_C(5) +
        (uint64_t)(flags & UINT32_C(0xff));
}

static int rune_corpus_run_c07_c08(
    uint64_t seed,
    uint64_t working_set_bytes,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    uint64_t count;
    rune_corpus_record *records;
    uint32_t *x;
    uint32_t *y;
    uint32_t *flags;
    uint64_t i;
    uint64_t aos_result;
    uint64_t soa_result;
    uint64_t aos_bytes;
    uint64_t soa_bytes;

    count = working_set_bytes / (uint64_t)sizeof(rune_corpus_record);
    if (count == 0u) {
        count = 1u;
    }

    aos_bytes = count * (uint64_t)sizeof(rune_corpus_record);
    soa_bytes = count * UINT64_C(3) * (uint64_t)sizeof(uint32_t);

    records = (rune_corpus_record *)rune_corpus_malloc(aos_bytes);
    x = (uint32_t *)rune_corpus_malloc(
        count * (uint64_t)sizeof(uint32_t)
    );
    y = (uint32_t *)rune_corpus_malloc(
        count * (uint64_t)sizeof(uint32_t)
    );
    flags = (uint32_t *)rune_corpus_malloc(
        count * (uint64_t)sizeof(uint32_t)
    );

    if (records == NULL || x == NULL || y == NULL || flags == NULL) {
        free(records);
        free(x);
        free(y);
        free(flags);
        return 0;
    }

    for (i = 0u; i < count; ++i) {
        records[(size_t)i].x =
            rune_corpus_value32(seed ^ UINT64_C(0x11), i);
        records[(size_t)i].y =
            rune_corpus_value32(seed ^ UINT64_C(0x22), i);
        records[(size_t)i].flags =
            rune_corpus_value32(seed ^ UINT64_C(0x33), i);
        records[(size_t)i].cold0 =
            rune_corpus_value32(seed ^ UINT64_C(0x44), i);
        records[(size_t)i].cold1 =
            rune_corpus_value32(seed ^ UINT64_C(0x55), i);
        records[(size_t)i].cold2 =
            rune_corpus_value32(seed ^ UINT64_C(0x66), i);

        x[(size_t)i] = records[(size_t)i].x;
        y[(size_t)i] = records[(size_t)i].y;
        flags[(size_t)i] = records[(size_t)i].flags;
    }

    aos_result = 0u;
    soa_result = 0u;
    for (i = 0u; i < count; ++i) {
        aos_result += rune_corpus_hot_result(
            records[(size_t)i].x,
            records[(size_t)i].y,
            records[(size_t)i].flags
        );
        soa_result += rune_corpus_hot_result(
            x[(size_t)i],
            y[(size_t)i],
            flags[(size_t)i]
        );
    }

    if (aos_result != soa_result) {
        free(records);
        free(x);
        free(y);
        free(flags);
        return 0;
    }

    if (!rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C07-aos-hot-traversal",
            seed,
            count,
            aos_bytes,
            0u,
            aos_bytes,
            0u,
            aos_result,
            1u) ||
        !rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C08-soa-hot-fields",
            seed,
            count,
            soa_bytes,
            0u,
            soa_bytes,
            0u,
            soa_result,
            1u)) {
        free(records);
        free(x);
        free(y);
        free(flags);
        return 0;
    }

    free(records);
    free(x);
    free(y);
    free(flags);
    return 1;
}

static int rune_corpus_run_c09(
    uint64_t seed,
    uint64_t count,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    static const uint32_t tile_sizes[] = {
        32u, 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u
    };
    uint32_t tile[4096];
    uint64_t expected;
    uint64_t result;
    uint64_t index;
    uint64_t tile_count;
    uint64_t take;
    size_t variant;
    uint32_t variants_checked;

    expected = 0u;
    for (index = 0u; index < count; ++index) {
        expected += (uint64_t)rune_corpus_transform32(
            rune_corpus_value32(seed, index)
        );
    }

    variants_checked = 0u;
    for (variant = 0u;
         variant < sizeof(tile_sizes) / sizeof(tile_sizes[0]);
         ++variant) {
        uint32_t tile_size;

        tile_size = tile_sizes[variant];
        if ((uint64_t)tile_size > count) {
            continue;
        }

        result = 0u;
        index = 0u;
        while (index < count) {
            uint64_t lane;

            take = count - index;
            if (take > (uint64_t)tile_size) {
                take = (uint64_t)tile_size;
            }

            for (lane = 0u; lane < take; ++lane) {
                tile[(size_t)lane] =
                    rune_corpus_value32(seed, index + lane);
            }

            for (lane = 0u; lane < take; ++lane) {
                result += (uint64_t)rune_corpus_transform32(
                    tile[(size_t)lane]
                );
            }
            index += take;
        }

        if (result != expected) {
            return 0;
        }
        variants_checked += 1u;
    }

    if (variants_checked == 0u) {
        return 0;
    }

    tile_count = count < UINT64_C(4096) ?
        count :
        UINT64_C(4096);

    return rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C09-microtile-sweep",
        seed,
        count,
        tile_count * (uint64_t)sizeof(uint32_t),
        tile_count * (uint64_t)sizeof(uint32_t),
        count * (uint64_t)sizeof(uint32_t),
        count * (uint64_t)sizeof(uint32_t),
        expected,
        variants_checked
    );
}

static int rune_corpus_run_c10(
    uint64_t seed,
    uint64_t count,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    uint32_t ring[256];
    uint64_t produced;
    uint64_t consumed;
    uint64_t expected;
    uint64_t result;
    uint32_t read_index;
    uint32_t write_index;
    uint32_t size;
    uint32_t pop_count;
    uint32_t lane;

    expected = 0u;
    for (produced = 0u; produced < count; ++produced) {
        expected += (uint64_t)rune_corpus_value32(seed, produced);
    }

    produced = 0u;
    consumed = 0u;
    result = 0u;
    read_index = 0u;
    write_index = 0u;
    size = 0u;

    while (consumed < count) {
        while (produced < count && size < 256u) {
            ring[write_index] = rune_corpus_value32(seed, produced);
            write_index = (write_index + 1u) & 255u;
            size += 1u;
            produced += 1u;
        }

        pop_count = size;
        if (pop_count > 129u) {
            pop_count = 129u;
        }

        for (lane = 0u; lane < pop_count; ++lane) {
            result += (uint64_t)ring[read_index];
            read_index = (read_index + 1u) & 255u;
            size -= 1u;
            consumed += 1u;
        }
    }

    if (result != expected || size != 0u || produced != count) {
        return 0;
    }

    return rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C10-caller-owned-ring",
        seed,
        count,
        (uint64_t)sizeof(ring),
        (uint64_t)sizeof(ring),
        count * (uint64_t)sizeof(uint32_t),
        count * (uint64_t)sizeof(uint32_t),
        result,
        1u
    );
}

static int rune_corpus_run_c11(
    uint64_t seed,
    uint64_t working_set_bytes,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    uint64_t arena_bytes;
    uint8_t *backing;
    rune_region region;
    rune_span storage;
    rune_arena arena;
    rune_span span;
    rune_status status;
    uint64_t round;
    uint64_t allocation_index;
    uint64_t result;
    uint64_t lane;
    uint8_t fill_value;

    arena_bytes = working_set_bytes;
    if (arena_bytes > UINT64_C(65536)) {
        arena_bytes = UINT64_C(65536);
    }
    if (arena_bytes < UINT64_C(4096)) {
        arena_bytes = UINT64_C(4096);
    }

    backing = (uint8_t *)rune_corpus_malloc(arena_bytes);
    if (backing == NULL) {
        return 0;
    }

    status = rune_region_attach(
        &region,
        backing,
        arena_bytes,
        RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
    );
    if (status != RUNE_OK) {
        free(backing);
        return 0;
    }

    status = rune_span_init(
        &storage,
        &region,
        0u,
        arena_bytes,
        RUNE_ACCESS_READ | RUNE_ACCESS_WRITE
    );
    if (status != RUNE_OK) {
        free(backing);
        return 0;
    }

    status = rune_arena_init(&arena, &storage);
    if (status != RUNE_OK) {
        free(backing);
        return 0;
    }

    result = 0u;
    allocation_index = 0u;
    for (round = 0u; round < UINT64_C(4); ++round) {
        for (;;) {
            status = rune_arena_alloc(
                &arena,
                UINT64_C(37),
                UINT64_C(64),
                &span
            );
            if (status == RUNE_ERR_EXHAUSTED) {
                break;
            }
            if (status != RUNE_OK) {
                free(backing);
                return 0;
            }

            fill_value = (uint8_t)(
                rune_corpus_value32(
                    seed ^ round,
                    allocation_index
                ) & UINT32_C(0xff)
            );
            status = rune_span_fill(&span, fill_value);
            if (status != RUNE_OK) {
                free(backing);
                return 0;
            }

            for (lane = 0u; lane < span.length; ++lane) {
                result += (uint64_t)backing[
                    (size_t)(span.offset + lane)
                ];
            }
            allocation_index += 1u;
        }

        status = rune_arena_reset(&arena);
        if (status != RUNE_OK) {
            free(backing);
            return 0;
        }
    }

    if (arena.high_water > arena_bytes ||
        arena.cumulative_consumed_bytes <= arena.high_water) {
        free(backing);
        return 0;
    }

    if (!rune_corpus_add_receipt(
            receipts,
            receipt_count,
            "C11-arena-reset",
            seed,
            allocation_index,
            arena_bytes,
            arena.high_water,
            arena.cumulative_payload_bytes,
            arena.cumulative_payload_bytes,
            result,
            4u)) {
        free(backing);
        return 0;
    }

    free(backing);
    return 1;
}

static int rune_corpus_run_c12(
    uint64_t seed,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    int32_t full[4096];
    rune_q16_16 bases[1024];
    rune_q16_16 step;
    rune_q16_16 value;
    rune_q16_16 reconstructed;
    uint64_t full_hash;
    uint64_t compact_hash;
    uint32_t i;
    uint32_t lane;
    rune_status status;

    (void)seed;
    step.raw = 257;

    for (i = 0u; i < 4096u; ++i) {
        full[i] = -65536 + (int32_t)(i * 257u);
    }

    for (i = 0u; i < 1024u; ++i) {
        bases[i].raw = full[i * 4u];
    }

    full_hash = UINT64_C(1469598103934665603);
    compact_hash = UINT64_C(1469598103934665603);

    for (i = 0u; i < 4096u; ++i) {
        full_hash = rune_corpus_hash_u64(
            full_hash,
            (uint64_t)(uint32_t)full[i]
        );
    }

    for (i = 0u; i < 1024u; ++i) {
        for (lane = 0u; lane < 4u; ++lane) {
            reconstructed = bases[i];
            value = step;
            while (value.raw != 0 && lane != 0u) {
                uint32_t n;

                for (n = 0u; n < lane; ++n) {
                    status = rune_q16_16_add(
                        reconstructed,
                        step,
                        &reconstructed
                    );
                    if (status != RUNE_OK) {
                        return 0;
                    }
                }
                value.raw = 0;
            }

            compact_hash = rune_corpus_hash_u64(
                compact_hash,
                (uint64_t)(uint32_t)reconstructed.raw
            );
        }
    }

    if (full_hash != compact_hash) {
        return 0;
    }

    return rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C12-fixed-point-reconstruction",
        seed,
        UINT64_C(4096),
        (uint64_t)sizeof(bases),
        (uint64_t)sizeof(rune_q16_16),
        (uint64_t)sizeof(bases),
        0u,
        compact_hash,
        2u
    );
}

static int rune_corpus_run_c13(
    uint64_t seed,
    uint64_t count,
    const uint32_t *retained,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    uint64_t retained_result;
    uint64_t regenerated_result;
    uint64_t pass;
    uint64_t i;
    uint64_t input_bytes;

    retained_result = 0u;
    regenerated_result = 0u;
    for (pass = 0u; pass < UINT64_C(4); ++pass) {
        for (i = 0u; i < count; ++i) {
            retained_result += (uint64_t)retained[(size_t)i];
            regenerated_result +=
                (uint64_t)rune_corpus_value32(seed, i);
        }
    }

    if (retained_result != regenerated_result) {
        return 0;
    }

    input_bytes = count * (uint64_t)sizeof(uint32_t);

    return rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C13-retain",
        seed,
        count * UINT64_C(4),
        input_bytes,
        0u,
        input_bytes * UINT64_C(4),
        0u,
        retained_result,
        1u
    ) &&
    rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C13-regenerate",
        seed,
        count * UINT64_C(4),
        UINT64_C(16),
        0u,
        0u,
        0u,
        regenerated_result,
        1u
    );
}

static uint8_t rune_corpus_document_byte(uint64_t seed, uint64_t index)
{
    static const uint8_t pattern[] = {
        '<', 'a', ' ', 'x', '=', '1', '2', '3', '>',
        'a', 'b', 'c', ' ', '\n',
        0xc3u, 0xa9u,
        '<', '/', 'a', '>', '\t',
        '<', 'p', '>', '9', '8', '7', '<', '/', 'p', '>', '\n'
    };
    uint64_t shifted;

    shifted = index + (seed % (uint64_t)sizeof(pattern));
    return pattern[(size_t)(shifted % (uint64_t)sizeof(pattern))];
}

static void rune_corpus_classify_byte(
    rune_corpus_counts *counts,
    uint8_t byte
)
{
    if (byte == (uint8_t)'<') {
        counts->lt += 1u;
    } else if (byte == (uint8_t)'>') {
        counts->gt += 1u;
    } else if (byte == (uint8_t)' ' ||
               byte == (uint8_t)'\n' ||
               byte == (uint8_t)'\t' ||
               byte == (uint8_t)'\r') {
        counts->whitespace += 1u;
    } else if (byte >= (uint8_t)'0' &&
               byte <= (uint8_t)'9') {
        counts->digit += 1u;
    } else if ((byte & (uint8_t)0x80u) != 0u) {
        counts->high += 1u;
    } else {
        counts->other += 1u;
    }
}

static uint64_t rune_corpus_counts_hash(
    const rune_corpus_counts *counts
)
{
    uint64_t hash;

    hash = UINT64_C(1469598103934665603);
    hash = rune_corpus_hash_u64(hash, counts->lt);
    hash = rune_corpus_hash_u64(hash, counts->gt);
    hash = rune_corpus_hash_u64(hash, counts->whitespace);
    hash = rune_corpus_hash_u64(hash, counts->digit);
    hash = rune_corpus_hash_u64(hash, counts->high);
    hash = rune_corpus_hash_u64(hash, counts->other);
    return hash;
}

static int rune_corpus_run_c14(
    uint64_t seed,
    uint64_t working_set_bytes,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    static const uint32_t chunk_sizes[] = { 7u, 31u, 257u };
    uint8_t chunk[257];
    rune_corpus_counts reference;
    rune_corpus_counts chunked;
    uint64_t reference_hash;
    uint64_t chunked_hash;
    uint64_t index;
    size_t variant;
    uint32_t variants_checked;

    memset(&reference, 0, sizeof(reference));
    for (index = 0u; index < working_set_bytes; ++index) {
        rune_corpus_classify_byte(
            &reference,
            rune_corpus_document_byte(seed, index)
        );
    }
    reference_hash = rune_corpus_counts_hash(&reference);

    variants_checked = 0u;
    for (variant = 0u;
         variant < sizeof(chunk_sizes) / sizeof(chunk_sizes[0]);
         ++variant) {
        uint32_t chunk_size;

        chunk_size = chunk_sizes[variant];
        memset(&chunked, 0, sizeof(chunked));
        index = 0u;

        while (index < working_set_bytes) {
            uint64_t take;
            uint64_t lane;

            take = working_set_bytes - index;
            if (take > (uint64_t)chunk_size) {
                take = (uint64_t)chunk_size;
            }

            for (lane = 0u; lane < take; ++lane) {
                chunk[(size_t)lane] =
                    rune_corpus_document_byte(seed, index + lane);
            }

            for (lane = 0u; lane < take; ++lane) {
                rune_corpus_classify_byte(
                    &chunked,
                    chunk[(size_t)lane]
                );
            }

            index += take;
        }

        chunked_hash = rune_corpus_counts_hash(&chunked);
        if (chunked_hash != reference_hash) {
            return 0;
        }
        variants_checked += 1u;
    }

    return rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C14-rivet-like-byte-stream",
        seed,
        working_set_bytes,
        (uint64_t)sizeof(chunk),
        (uint64_t)sizeof(chunk),
        working_set_bytes,
        working_set_bytes,
        reference_hash,
        variants_checked
    );
}

static int rune_corpus_run_c15(
    uint64_t seed,
    rune_corpus_receipt *receipts,
    size_t *receipt_count
)
{
    const uint64_t logical_items = UINT64_C(1) << 40u;
    const uint64_t window_items = UINT64_C(64);
    const uint64_t window_count = UINT64_C(64);
    uint64_t hash;
    uint64_t window;
    uint64_t lane;

    hash = UINT64_C(1469598103934665603);
    for (window = 0u; window < window_count; ++window) {
        uint64_t start;

        start = (
            (uint64_t)rune_corpus_value32(seed, window) *
            UINT64_C(2654435761)
        ) % (logical_items - window_items);

        hash = rune_corpus_hash_u64(hash, start);
        for (lane = 0u; lane < window_items; ++lane) {
            hash = rune_corpus_hash_u64(
                hash,
                (uint64_t)rune_corpus_value32(
                    seed,
                    start + lane
                )
            );
        }
    }

    return rune_corpus_add_receipt(
        receipts,
        receipt_count,
        "C15-huge-logical-domain",
        seed,
        logical_items,
        UINT64_C(48),
        UINT64_C(48),
        window_count * window_items * (uint64_t)sizeof(uint32_t),
        0u,
        hash,
        1u
    );
}

static int rune_corpus_run_size(
    const char *profile,
    uint64_t working_set_bytes,
    uint64_t seed
)
{
    uint64_t count;
    uint32_t *values;
    rune_corpus_receipt receipts[RUNE_CORPUS_MAX_RECEIPTS];
    size_t receipt_count;
    size_t i;

    if (!rune_corpus_is_power_of_two_u64(working_set_bytes) ||
        working_set_bytes < UINT64_C(4096) ||
        working_set_bytes > RUNE_CORPUS_MAX_BYTES ||
        (working_set_bytes % (uint64_t)sizeof(uint32_t)) != 0u) {
        fprintf(stderr, "invalid working-set size: %" PRIu64 "\n",
                working_set_bytes);
        return 0;
    }

    count = working_set_bytes / (uint64_t)sizeof(uint32_t);
    if (!rune_corpus_is_power_of_two_u64(count)) {
        fprintf(stderr, "element count must be a power of two\n");
        return 0;
    }

    if (!rune_corpus_build_values(seed, count, &values)) {
        fprintf(stderr, "could not build bounded procedural input\n");
        return 0;
    }

    receipt_count = 0u;

    if (!rune_corpus_run_c01_to_c06(
            seed,
            count,
            values,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c07_c08(
            seed,
            working_set_bytes,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c09(
            seed,
            count,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c10(
            seed,
            count,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c11(
            seed,
            working_set_bytes,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c12(
            seed,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c13(
            seed,
            count,
            values,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c14(
            seed,
            working_set_bytes,
            receipts,
            &receipt_count) ||
        !rune_corpus_run_c15(
            seed,
            receipts,
            &receipt_count)) {
        free(values);
        fprintf(stderr, "corpus correctness contract failed\n");
        return 0;
    }

    for (i = 0u; i < receipt_count; ++i) {
        rune_corpus_emit_receipt(
            profile,
            working_set_bytes,
            &receipts[i]
        );
    }
    rune_corpus_emit_summary(
        profile,
        working_set_bytes,
        seed,
        receipts,
        receipt_count
    );

    free(values);
    return 1;
}

static int rune_corpus_parse_u64(
    const char *text,
    uint64_t *out
)
{
    char *end;
    unsigned long long parsed;

    if (text == NULL || out == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    end = NULL;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (uint64_t)parsed;
    if ((unsigned long long)*out != parsed) {
        return 0;
    }

    return 1;
}

static void rune_corpus_usage(const char *program)
{
    fprintf(
        stderr,
        "usage: %s [--profile smoke|local] [--bytes N] [--seed N]\n",
        program
    );
}

int main(int argc, char **argv)
{
    static const uint64_t local_ladder[] = {
        UINT64_C(4096),
        UINT64_C(32768),
        UINT64_C(262144),
        UINT64_C(1048576),
        UINT64_C(4194304),
        UINT64_C(16777216),
        UINT64_C(67108864)
    };
    const char *profile;
    uint64_t seed;
    uint64_t explicit_bytes;
    int has_explicit_bytes;
    int i;

    profile = "smoke";
    seed = RUNE_CORPUS_DEFAULT_SEED;
    explicit_bytes = 0u;
    has_explicit_bytes = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                rune_corpus_usage(argv[0]);
                return 2;
            }
            profile = argv[++i];
            if (strcmp(profile, "smoke") != 0 &&
                strcmp(profile, "local") != 0) {
                rune_corpus_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--bytes") == 0) {
            if (i + 1 >= argc ||
                !rune_corpus_parse_u64(
                    argv[++i],
                    &explicit_bytes)) {
                rune_corpus_usage(argv[0]);
                return 2;
            }
            has_explicit_bytes = 1;
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (i + 1 >= argc ||
                !rune_corpus_parse_u64(argv[++i], &seed)) {
                rune_corpus_usage(argv[0]);
                return 2;
            }
        } else {
            rune_corpus_usage(argv[0]);
            return 2;
        }
    }

    if (has_explicit_bytes) {
        if (!rune_corpus_run_size(
                "explicit",
                explicit_bytes,
                seed)) {
            return 1;
        }
        return 0;
    }

    if (strcmp(profile, "smoke") == 0) {
        return rune_corpus_run_size(
            "smoke",
            RUNE_CORPUS_SMOKE_BYTES,
            seed
        ) ? 0 : 1;
    }

    {
        size_t lane;

        for (lane = 0u;
             lane < sizeof(local_ladder) / sizeof(local_ladder[0]);
             ++lane) {
            if (!rune_corpus_run_size(
                    "local",
                    local_ladder[lane],
                    seed)) {
                return 1;
            }
        }
    }

    return 0;
}
