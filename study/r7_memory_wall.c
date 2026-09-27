/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/arena.h"
#include "rune/region.h"
#include "rune/status.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define R7_SEED UINT64_C(303)
#define R7_TARGET_BYTES UINT64_C(67108864)
#define R7_MIN_BYTES UINT64_C(4096)
#define R7_MAX_BYTES UINT64_C(67108864)
#define R7_AOS_LOGICAL_BYTES UINT64_C(24)
#define R7_HOT_LOGICAL_BYTES UINT64_C(12)
#define R7_LOOKUP_ENTRIES UINT64_C(4096)

#define R7_COMPARE_LOCALITY UINT64_C(1)
#define R7_COMPARE_MATERIALIZATION UINT64_C(2)
#define R7_COMPARE_LAYOUT UINT64_C(3)
#define R7_COMPARE_MICROTILE UINT64_C(4)
#define R7_COMPARE_RETAIN_REGENERATE UINT64_C(5)
#define R7_COMPARE_LOOKUP_RECOMPUTE UINT64_C(6)
#define R7_COMPARE_PEAK_CUMULATIVE UINT64_C(7)
#define R7_COMPARE_LOGICAL_RESIDENT UINT64_C(8)

#define R7_VARIANT_A UINT64_C(1)
#define R7_VARIANT_B UINT64_C(2)

typedef struct r7_observation {
    uint64_t comparison_id;
    uint64_t variant_id;
    uint64_t working_set_bytes;
    uint64_t repeat_index;
    uint64_t logical_items;
    uint64_t resident_bytes;
    uint64_t peak_live_bytes;
    uint64_t cumulative_traffic_bytes;
    uint64_t model_bytes_read;
    uint64_t model_bytes_written;
    uint64_t result_u64;
    uint64_t setup_ticks;
    uint64_t execute_ticks;
    uint64_t teardown_ticks;
    uint64_t total_ticks;
    uint64_t clock_ticks_per_second;
    uint64_t lifecycle_complete;
} r7_observation;

static volatile uint64_t r7_sink = UINT64_C(0);

static uint32_t r7_value32(uint64_t seed, uint64_t index)
{
    uint64_t z;

    z = index + seed + UINT64_C(0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30u)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27u)) * UINT64_C(0x94d049bb133111eb);
    z ^= z >> 31u;
    return (uint32_t)(z >> 32u);
}

static uint32_t r7_transform32(uint32_t value)
{
    return value * UINT32_C(1664525) + UINT32_C(1013904223);
}

static uint64_t r7_hash_u64(uint64_t hash, uint64_t value)
{
    uint32_t lane;

    for (lane = 0u; lane < 8u; ++lane) {
        hash ^= value & UINT64_C(0xff);
        hash *= UINT64_C(1099511628211);
        value >>= 8u;
    }

    return hash;
}

static uint64_t r7_hot_result(uint32_t x, uint32_t y, uint32_t flags)
{
    return (uint64_t)x * UINT64_C(3) +
        (uint64_t)y * UINT64_C(5) +
        (uint64_t)(flags & UINT32_C(0xff));
}

static int r7_is_power_of_two_u64(uint64_t value)
{
    return value != 0u && (value & (value - UINT64_C(1))) == 0u;
}

static void *r7_malloc(uint64_t bytes)
{
    size_t host_bytes;

    host_bytes = (size_t)bytes;
    if ((uint64_t)host_bytes != bytes) {
        return NULL;
    }

    return malloc(host_bytes);
}

static uint64_t r7_rounds_for_bytes(uint64_t bytes)
{
    uint64_t rounds;

    if (bytes == 0u) {
        return UINT64_C(1);
    }

    rounds = R7_TARGET_BYTES / bytes;
    if (rounds == 0u) {
        rounds = UINT64_C(1);
    }
    if (rounds > UINT64_C(16384)) {
        rounds = UINT64_C(16384);
    }

    return rounds;
}

static int r7_now(clock_t *out)
{
    clock_t value;

    if (out == NULL) {
        return 0;
    }

    value = clock();
    if (value == (clock_t)-1) {
        return 0;
    }

    *out = value;
    return 1;
}

static int r7_ticks_between(
    clock_t start,
    clock_t end,
    uint64_t *out
)
{
    clock_t delta;
    uint64_t converted;

    if (out == NULL) {
        return 0;
    }

    if (end < start) {
        return 0;
    }

    delta = end - start;

    converted = (uint64_t)delta;
    if ((clock_t)converted != delta) {
        return 0;
    }

    *out = converted;
    return 1;
}

static int r7_finish_times(
    r7_observation *observation,
    clock_t total_start,
    clock_t setup_end,
    clock_t execute_end,
    clock_t teardown_end
)
{
    if (!r7_ticks_between(
            total_start,
            setup_end,
            &observation->setup_ticks) ||
        !r7_ticks_between(
            setup_end,
            execute_end,
            &observation->execute_ticks) ||
        !r7_ticks_between(
            execute_end,
            teardown_end,
            &observation->teardown_ticks) ||
        !r7_ticks_between(
            total_start,
            teardown_end,
            &observation->total_ticks)) {
        return 0;
    }

    observation->clock_ticks_per_second = (uint64_t)CLOCKS_PER_SEC;
    observation->lifecycle_complete = UINT64_C(1);
    return 1;
}

static void r7_observation_begin(
    r7_observation *observation,
    uint64_t comparison_id,
    uint64_t variant_id,
    uint64_t working_set_bytes,
    uint64_t repeat_index
)
{
    memset(observation, 0, sizeof(*observation));
    observation->comparison_id = comparison_id;
    observation->variant_id = variant_id;
    observation->working_set_bytes = working_set_bytes;
    observation->repeat_index = repeat_index;
}

static int r7_write_byte(uint8_t value)
{
    return fwrite(&value, 1u, 1u, stdout) == 1u;
}

static int r7_write_u64(uint64_t value)
{
    uint8_t digits[20];
    size_t count;
    size_t i;

    count = 0u;
    do {
        digits[count] = (uint8_t)(
            0x30u + (uint8_t)(value % UINT64_C(10))
        );
        value /= UINT64_C(10);
        count += 1u;
    } while (value != 0u);

    for (i = count; i != 0u; --i) {
        if (!r7_write_byte(digits[i - 1u])) {
            return 0;
        }
    }

    return 1;
}

static int r7_write_field(uint64_t value, int last)
{
    if (!r7_write_u64(value)) {
        return 0;
    }

    return r7_write_byte(
        last ? (uint8_t)0x0au : (uint8_t)0x09u
    );
}

static int r7_emit(const r7_observation *observation)
{
    return
        r7_write_field(observation->comparison_id, 0) &&
        r7_write_field(observation->variant_id, 0) &&
        r7_write_field(observation->working_set_bytes, 0) &&
        r7_write_field(observation->repeat_index, 0) &&
        r7_write_field(observation->logical_items, 0) &&
        r7_write_field(observation->resident_bytes, 0) &&
        r7_write_field(observation->peak_live_bytes, 0) &&
        r7_write_field(observation->cumulative_traffic_bytes, 0) &&
        r7_write_field(observation->model_bytes_read, 0) &&
        r7_write_field(observation->model_bytes_written, 0) &&
        r7_write_field(observation->result_u64, 0) &&
        r7_write_field(observation->setup_ticks, 0) &&
        r7_write_field(observation->execute_ticks, 0) &&
        r7_write_field(observation->teardown_ticks, 0) &&
        r7_write_field(observation->total_ticks, 0) &&
        r7_write_field(observation->clock_ticks_per_second, 0) &&
        r7_write_field(observation->lifecycle_complete, 1);
}

static int r7_fill_values(
    uint32_t *values,
    uint64_t count,
    uint64_t seed
)
{
    uint64_t i;

    if (values == NULL && count != 0u) {
        return 0;
    }

    for (i = 0u; i < count; ++i) {
        values[(size_t)i] = r7_value32(seed, i);
    }

    return 1;
}

static int r7_measure_locality(
    uint64_t variant,
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    uint64_t count;
    uint64_t rounds;
    uint64_t round;
    uint64_t i;
    uint64_t result;
    uint64_t mask;
    uint64_t stride;
    uint32_t *values;
    volatile const uint32_t *read_values;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    count = working_set_bytes / (uint64_t)sizeof(uint32_t);
    rounds = r7_rounds_for_bytes(working_set_bytes);
    mask = count - UINT64_C(1);
    stride = ((R7_SEED << 1u) | UINT64_C(1)) & mask;
    if (stride == 0u) {
        stride = UINT64_C(1);
    }

    r7_observation_begin(
        observation,
        R7_COMPARE_LOCALITY,
        variant,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    values = (uint32_t *)r7_malloc(working_set_bytes);
    if (values == NULL ||
        !r7_fill_values(values, count, R7_SEED)) {
        free(values);
        return 0;
    }
    read_values = values;

    if (!r7_now(&setup_end)) {
        free(values);
        return 0;
    }

    result = 0u;
    if (variant == R7_VARIANT_A) {
        volatile const uint32_t *read_values;

        read_values = values;
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                result += (uint64_t)read_values[(size_t)i];
            }
        }
    } else {
        for (round = 0u; round < rounds; ++round) {
            uint64_t index;

            index = round & mask;
            for (i = 0u; i < count; ++i) {
                result += (uint64_t)read_values[(size_t)index];
                index = (index + stride) & mask;
            }
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
        free(values);
        return 0;
    }

    free(values);
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = count * rounds;
    observation->resident_bytes = working_set_bytes;
    observation->peak_live_bytes = working_set_bytes;
    observation->cumulative_traffic_bytes =
        working_set_bytes * rounds;
    observation->model_bytes_read =
        working_set_bytes * rounds;
    observation->model_bytes_written = 0u;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_materialization(
    uint64_t variant,
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    uint64_t count;
    uint64_t rounds;
    uint64_t round;
    uint64_t i;
    uint64_t result;
    uint32_t *values;
    uint32_t *temporary;
    volatile const uint32_t *read_values;
    volatile uint32_t *temporary_view;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    count = working_set_bytes / (uint64_t)sizeof(uint32_t);
    rounds = r7_rounds_for_bytes(working_set_bytes);

    r7_observation_begin(
        observation,
        R7_COMPARE_MATERIALIZATION,
        variant,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    values = (uint32_t *)r7_malloc(working_set_bytes);
    temporary = NULL;
    temporary_view = NULL;
    if (values == NULL ||
        !r7_fill_values(values, count, R7_SEED)) {
        free(values);
        return 0;
    }

    if (variant == R7_VARIANT_A) {
        temporary = (uint32_t *)r7_malloc(working_set_bytes);
        if (temporary == NULL) {
            free(values);
            return 0;
        }
    }

    if (!r7_now(&setup_end)) {
        free(temporary);
        free(values);
        return 0;
    }

    read_values = values;
    temporary_view = temporary;
    result = 0u;
    if (variant == R7_VARIANT_A) {
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                temporary_view[(size_t)i] =
                    r7_transform32(read_values[(size_t)i]);
            }
            for (i = 0u; i < count; ++i) {
                result += (uint64_t)temporary_view[(size_t)i];
            }
        }
    } else {
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                result += (uint64_t)r7_transform32(
                    read_values[(size_t)i]
                );
            }
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
        free(temporary);
        free(values);
        return 0;
    }

    free(temporary);
    free(values);
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = count * rounds;
    if (variant == R7_VARIANT_A) {
        observation->resident_bytes =
            working_set_bytes * UINT64_C(2);
        observation->peak_live_bytes =
            working_set_bytes * UINT64_C(2);
        observation->model_bytes_read =
            working_set_bytes * rounds * UINT64_C(2);
        observation->model_bytes_written =
            working_set_bytes * rounds;
    } else {
        observation->resident_bytes = working_set_bytes;
        observation->peak_live_bytes = working_set_bytes;
        observation->model_bytes_read =
            working_set_bytes * rounds;
        observation->model_bytes_written = 0u;
    }
    observation->cumulative_traffic_bytes =
        observation->model_bytes_read +
        observation->model_bytes_written;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_layout(
    uint64_t variant,
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    uint64_t count;
    uint64_t rounds;
    uint64_t round;
    uint64_t i;
    uint64_t result;
    uint64_t hot_bytes;
    uint64_t aos_bytes;
    uint32_t *records;
    uint32_t *x;
    uint32_t *y;
    uint32_t *flags;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    count = working_set_bytes / R7_AOS_LOGICAL_BYTES;
    if (count == 0u) {
        return 0;
    }
    rounds = r7_rounds_for_bytes(working_set_bytes);
    hot_bytes = count * R7_HOT_LOGICAL_BYTES;
    aos_bytes = count * R7_AOS_LOGICAL_BYTES;

    records = NULL;
    x = NULL;
    y = NULL;
    flags = NULL;

    r7_observation_begin(
        observation,
        R7_COMPARE_LAYOUT,
        variant,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    if (variant == R7_VARIANT_A) {
        records = (uint32_t *)r7_malloc(
            count * UINT64_C(6) * (uint64_t)sizeof(uint32_t)
        );
        if (records == NULL) {
            return 0;
        }

        for (i = 0u; i < count; ++i) {
            size_t base;

            base = (size_t)(i * UINT64_C(6));
            records[base + 0u] =
                r7_value32(R7_SEED ^ UINT64_C(0x11), i);
            records[base + 1u] =
                r7_value32(R7_SEED ^ UINT64_C(0x22), i);
            records[base + 2u] =
                r7_value32(R7_SEED ^ UINT64_C(0x33), i);
            records[base + 3u] =
                r7_value32(R7_SEED ^ UINT64_C(0x44), i);
            records[base + 4u] =
                r7_value32(R7_SEED ^ UINT64_C(0x55), i);
            records[base + 5u] =
                r7_value32(R7_SEED ^ UINT64_C(0x66), i);
        }
    } else {
        x = (uint32_t *)r7_malloc(
            count * (uint64_t)sizeof(uint32_t)
        );
        y = (uint32_t *)r7_malloc(
            count * (uint64_t)sizeof(uint32_t)
        );
        flags = (uint32_t *)r7_malloc(
            count * (uint64_t)sizeof(uint32_t)
        );

        if (x == NULL || y == NULL || flags == NULL) {
            free(x);
            free(y);
            free(flags);
            return 0;
        }

        for (i = 0u; i < count; ++i) {
            x[(size_t)i] =
                r7_value32(R7_SEED ^ UINT64_C(0x11), i);
            y[(size_t)i] =
                r7_value32(R7_SEED ^ UINT64_C(0x22), i);
            flags[(size_t)i] =
                r7_value32(R7_SEED ^ UINT64_C(0x33), i);
        }
    }

    if (!r7_now(&setup_end)) {
        free(records);
        free(x);
        free(y);
        free(flags);
        return 0;
    }

    result = 0u;
    if (variant == R7_VARIANT_A) {
        volatile const uint32_t *read_records;

        read_records = records;
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                size_t base;

                base = (size_t)(i * UINT64_C(6));
                result += r7_hot_result(
                    read_records[base + 0u],
                    read_records[base + 1u],
                    read_records[base + 2u]
                );
            }
        }
    } else {
        volatile const uint32_t *read_x;
        volatile const uint32_t *read_y;
        volatile const uint32_t *read_flags;

        read_x = x;
        read_y = y;
        read_flags = flags;
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                result += r7_hot_result(
                    read_x[(size_t)i],
                    read_y[(size_t)i],
                    read_flags[(size_t)i]
                );
            }
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
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
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = count * rounds;
    observation->resident_bytes =
        variant == R7_VARIANT_A ? aos_bytes : hot_bytes;
    observation->peak_live_bytes = observation->resident_bytes;
    observation->model_bytes_read = hot_bytes * rounds;
    observation->model_bytes_written = 0u;
    observation->cumulative_traffic_bytes =
        observation->model_bytes_read;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_microtile(
    uint64_t tile_size,
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    uint32_t *tile_storage;
    volatile uint32_t *tile;
    uint64_t tile_bytes;
    uint64_t count;
    uint64_t rounds;
    uint64_t round;
    uint64_t index;
    uint64_t result;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    if (tile_size == 0u || tile_size > UINT64_C(4096)) {
        return 0;
    }

    count = working_set_bytes / (uint64_t)sizeof(uint32_t);
    rounds = r7_rounds_for_bytes(working_set_bytes);
    tile_bytes = tile_size * (uint64_t)sizeof(uint32_t);

    r7_observation_begin(
        observation,
        R7_COMPARE_MICROTILE,
        tile_size,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    tile_storage = (uint32_t *)r7_malloc(tile_bytes);
    if (tile_storage == NULL) {
        return 0;
    }
    tile = tile_storage;

    if (!r7_now(&setup_end)) {
        free(tile_storage);
        return 0;
    }

    result = 0u;
    for (round = 0u; round < rounds; ++round) {
        index = 0u;
        while (index < count) {
            uint64_t take;
            uint64_t lane;

            take = count - index;
            if (take > tile_size) {
                take = tile_size;
            }

            for (lane = 0u; lane < take; ++lane) {
                tile[(size_t)lane] =
                    r7_value32(R7_SEED, index + lane);
            }

            for (lane = 0u; lane < take; ++lane) {
                result += (uint64_t)r7_transform32(
                    tile[(size_t)lane]
                );
            }

            index += take;
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
        free(tile_storage);
        return 0;
    }

    free(tile_storage);
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = count * rounds;
    observation->resident_bytes = tile_bytes;
    observation->peak_live_bytes = tile_bytes;
    observation->model_bytes_read =
        working_set_bytes * rounds;
    observation->model_bytes_written =
        working_set_bytes * rounds;
    observation->cumulative_traffic_bytes =
        observation->model_bytes_read +
        observation->model_bytes_written;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_retain_regenerate(
    uint64_t variant,
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    uint64_t count;
    uint64_t rounds;
    uint64_t passes;
    uint64_t pass;
    uint64_t i;
    uint64_t result;
    uint32_t *values;
    volatile uint64_t regenerate_seed;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    count = working_set_bytes / (uint64_t)sizeof(uint32_t);
    rounds = r7_rounds_for_bytes(working_set_bytes);
    passes = rounds * UINT64_C(4);
    values = NULL;
    regenerate_seed = R7_SEED;

    r7_observation_begin(
        observation,
        R7_COMPARE_RETAIN_REGENERATE,
        variant,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    if (variant == R7_VARIANT_A) {
        values = (uint32_t *)r7_malloc(working_set_bytes);
        if (values == NULL ||
            !r7_fill_values(values, count, R7_SEED)) {
            free(values);
            return 0;
        }
    }

    if (!r7_now(&setup_end)) {
        free(values);
        return 0;
    }

    result = 0u;
    if (variant == R7_VARIANT_A) {
        volatile const uint32_t *read_values;

        read_values = values;
        for (pass = 0u; pass < passes; ++pass) {
            for (i = 0u; i < count; ++i) {
                result += (uint64_t)read_values[(size_t)i];
            }
        }
    } else {
        for (pass = 0u; pass < passes; ++pass) {
            for (i = 0u; i < count; ++i) {
                result += (uint64_t)r7_value32(regenerate_seed, i);
            }
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
        free(values);
        return 0;
    }

    free(values);
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = count * passes;
    observation->resident_bytes =
        variant == R7_VARIANT_A ?
        working_set_bytes :
        UINT64_C(16);
    observation->peak_live_bytes = observation->resident_bytes;
    observation->model_bytes_read =
        variant == R7_VARIANT_A ?
        working_set_bytes * passes :
        0u;
    observation->model_bytes_written = 0u;
    observation->cumulative_traffic_bytes =
        observation->model_bytes_read;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_lookup_recompute(
    uint64_t variant,
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    uint64_t count;
    uint64_t rounds;
    uint64_t round;
    uint64_t i;
    uint64_t index;
    uint64_t result;
    uint32_t *table;
    volatile uint64_t recompute_seed;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    count = working_set_bytes / (uint64_t)sizeof(uint32_t);
    rounds = r7_rounds_for_bytes(working_set_bytes);
    table = NULL;
    recompute_seed = R7_SEED;

    r7_observation_begin(
        observation,
        R7_COMPARE_LOOKUP_RECOMPUTE,
        variant,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    if (variant == R7_VARIANT_A) {
        table = (uint32_t *)r7_malloc(
            R7_LOOKUP_ENTRIES * (uint64_t)sizeof(uint32_t)
        );
        if (table == NULL ||
            !r7_fill_values(table, R7_LOOKUP_ENTRIES, R7_SEED)) {
            free(table);
            return 0;
        }
    }

    if (!r7_now(&setup_end)) {
        free(table);
        return 0;
    }

    result = 0u;
    if (variant == R7_VARIANT_A) {
        volatile const uint32_t *read_table;

        read_table = table;
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                index = i & (R7_LOOKUP_ENTRIES - UINT64_C(1));
                result += (uint64_t)read_table[(size_t)index];
            }
        }
    } else {
        for (round = 0u; round < rounds; ++round) {
            for (i = 0u; i < count; ++i) {
                index = i & (R7_LOOKUP_ENTRIES - UINT64_C(1));
                result += (uint64_t)r7_value32(recompute_seed, index);
            }
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
        free(table);
        return 0;
    }

    free(table);
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = count * rounds;
    observation->resident_bytes =
        variant == R7_VARIANT_A ?
        R7_LOOKUP_ENTRIES * (uint64_t)sizeof(uint32_t) :
        UINT64_C(16);
    observation->peak_live_bytes = observation->resident_bytes;
    observation->model_bytes_read =
        variant == R7_VARIANT_A ?
        count * rounds * (uint64_t)sizeof(uint32_t) :
        0u;
    observation->model_bytes_written = 0u;
    observation->cumulative_traffic_bytes =
        observation->model_bytes_read;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_arena_accounting(
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
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
    uint64_t lane;
    uint64_t result;
    uint8_t fill_value;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    arena_bytes = working_set_bytes;
    if (arena_bytes > UINT64_C(65536)) {
        arena_bytes = UINT64_C(65536);
    }
    if (arena_bytes < UINT64_C(4096)) {
        arena_bytes = UINT64_C(4096);
    }

    r7_observation_begin(
        observation,
        R7_COMPARE_PEAK_CUMULATIVE,
        R7_VARIANT_A,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start)) {
        return 0;
    }

    backing = (uint8_t *)r7_malloc(arena_bytes);
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
    if (status != RUNE_OK ||
        rune_arena_init(&arena, &storage) != RUNE_OK) {
        free(backing);
        return 0;
    }

    if (!r7_now(&setup_end)) {
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
                r7_value32(
                    R7_SEED ^ round,
                    allocation_index
                ) & UINT32_C(0xff)
            );
            if (rune_span_fill(&span, fill_value) != RUNE_OK) {
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

        if (rune_arena_reset(&arena) != RUNE_OK) {
            free(backing);
            return 0;
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end)) {
        free(backing);
        return 0;
    }

    free(backing);
    if (!r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = allocation_index;
    observation->resident_bytes = arena_bytes;
    observation->peak_live_bytes = arena.high_water;
    observation->cumulative_traffic_bytes =
        arena.cumulative_consumed_bytes;
    observation->model_bytes_read =
        arena.cumulative_payload_bytes;
    observation->model_bytes_written =
        arena.cumulative_payload_bytes;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_logical_resident(
    uint64_t working_set_bytes,
    uint64_t repeat_index,
    r7_observation *observation
)
{
    const uint64_t logical_items = UINT64_C(1) << 40u;
    const uint64_t window_items = UINT64_C(64);
    const uint64_t window_count = UINT64_C(64);
    uint64_t window;
    uint64_t lane;
    uint64_t result;
    clock_t total_start;
    clock_t setup_end;
    clock_t execute_end;
    clock_t teardown_end;

    r7_observation_begin(
        observation,
        R7_COMPARE_LOGICAL_RESIDENT,
        R7_VARIANT_A,
        working_set_bytes,
        repeat_index
    );

    if (!r7_now(&total_start) ||
        !r7_now(&setup_end)) {
        return 0;
    }

    result = UINT64_C(14695981039346656037);
    for (window = 0u; window < window_count; ++window) {
        uint64_t start;

        start = (
            (uint64_t)r7_value32(R7_SEED, window) *
            UINT64_C(2654435761)
        ) % (logical_items - window_items);

        result = r7_hash_u64(result, start);

        for (lane = 0u; lane < window_items; ++lane) {
            result = r7_hash_u64(
                result,
                (uint64_t)r7_value32(
                    R7_SEED,
                    start + lane
                )
            );
        }
    }

    r7_sink ^= result;
    if (!r7_now(&execute_end) ||
        !r7_now(&teardown_end)) {
        return 0;
    }

    observation->logical_items = logical_items;
    observation->resident_bytes = UINT64_C(48);
    observation->peak_live_bytes = UINT64_C(48);
    observation->cumulative_traffic_bytes = 0u;
    observation->model_bytes_read = 0u;
    observation->model_bytes_written = 0u;
    observation->result_u64 = result;

    return r7_finish_times(
        observation,
        total_start,
        setup_end,
        execute_end,
        teardown_end
    );
}

static int r7_measure_size(
    uint64_t working_set_bytes,
    uint64_t repeat_index
)
{
    static const uint64_t tiles[] = {
        UINT64_C(32),
        UINT64_C(64),
        UINT64_C(128),
        UINT64_C(256),
        UINT64_C(512),
        UINT64_C(1024),
        UINT64_C(2048),
        UINT64_C(4096)
    };
    r7_observation a;
    r7_observation b;
    r7_observation observation;
    uint64_t reference;
    int have_reference;
    size_t i;

    if (!r7_measure_locality(
            R7_VARIANT_A,
            working_set_bytes,
            repeat_index,
            &a) ||
        !r7_measure_locality(
            R7_VARIANT_B,
            working_set_bytes,
            repeat_index,
            &b) ||
        a.result_u64 != b.result_u64 ||
        !r7_emit(&a) ||
        !r7_emit(&b)) {
        return 0;
    }

    if (!r7_measure_materialization(
            R7_VARIANT_A,
            working_set_bytes,
            repeat_index,
            &a) ||
        !r7_measure_materialization(
            R7_VARIANT_B,
            working_set_bytes,
            repeat_index,
            &b) ||
        a.result_u64 != b.result_u64 ||
        !r7_emit(&a) ||
        !r7_emit(&b)) {
        return 0;
    }

    if (!r7_measure_layout(
            R7_VARIANT_A,
            working_set_bytes,
            repeat_index,
            &a) ||
        !r7_measure_layout(
            R7_VARIANT_B,
            working_set_bytes,
            repeat_index,
            &b) ||
        a.result_u64 != b.result_u64 ||
        !r7_emit(&a) ||
        !r7_emit(&b)) {
        return 0;
    }

    reference = 0u;
    have_reference = 0;
    for (i = 0u; i < sizeof(tiles) / sizeof(tiles[0]); ++i) {
        if (!r7_measure_microtile(
                tiles[i],
                working_set_bytes,
                repeat_index,
                &observation)) {
            return 0;
        }

        if (!have_reference) {
            reference = observation.result_u64;
            have_reference = 1;
        } else if (reference != observation.result_u64) {
            return 0;
        }

        if (!r7_emit(&observation)) {
            return 0;
        }
    }

    if (!r7_measure_retain_regenerate(
            R7_VARIANT_A,
            working_set_bytes,
            repeat_index,
            &a) ||
        !r7_measure_retain_regenerate(
            R7_VARIANT_B,
            working_set_bytes,
            repeat_index,
            &b) ||
        a.result_u64 != b.result_u64 ||
        !r7_emit(&a) ||
        !r7_emit(&b)) {
        return 0;
    }

    if (!r7_measure_lookup_recompute(
            R7_VARIANT_A,
            working_set_bytes,
            repeat_index,
            &a) ||
        !r7_measure_lookup_recompute(
            R7_VARIANT_B,
            working_set_bytes,
            repeat_index,
            &b) ||
        a.result_u64 != b.result_u64 ||
        !r7_emit(&a) ||
        !r7_emit(&b)) {
        return 0;
    }

    if (!r7_measure_arena_accounting(
            working_set_bytes,
            repeat_index,
            &observation) ||
        !r7_emit(&observation)) {
        return 0;
    }

    if (!r7_measure_logical_resident(
            working_set_bytes,
            repeat_index,
            &observation) ||
        !r7_emit(&observation)) {
        return 0;
    }

    return 1;
}

static int r7_parse_u64(const char *text, uint64_t *out)
{
    char *end;
    unsigned long long value;

    if (text == NULL || out == NULL || text[0] == '\0' ||
        text[0] == '-' ||
        text[0] == ' ' ||
        text[0] == '\t' ||
        text[0] == '\n' ||
        text[0] == '\r' ||
        text[0] == '\f' ||
        text[0] == '\v') {
        return 0;
    }

    errno = 0;
    end = NULL;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }

    *out = (uint64_t)value;
    return (unsigned long long)*out == value;
}

static void r7_usage(const char *program)
{
    fprintf(
        stderr,
        "usage: %s [--profile smoke|local] [--bytes N] [--repeats N]\n",
        program
    );
}

int main(int argc, char **argv)
{
    static const uint64_t local_sizes[] = {
        UINT64_C(4096),
        UINT64_C(32768),
        UINT64_C(262144),
        UINT64_C(1048576),
        UINT64_C(4194304),
        UINT64_C(16777216),
        UINT64_C(67108864)
    };
    const char *profile;
    uint64_t explicit_bytes;
    uint64_t repeats;
    int has_explicit_bytes;
    int repeats_explicit;
    int i;
    uint64_t repeat;

    profile = "smoke";
    explicit_bytes = 0u;
    repeats = UINT64_C(1);
    has_explicit_bytes = 0;
    repeats_explicit = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                r7_usage(argv[0]);
                return 2;
            }
            profile = argv[++i];
            if (strcmp(profile, "smoke") != 0 &&
                strcmp(profile, "local") != 0) {
                r7_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--bytes") == 0) {
            if (i + 1 >= argc ||
                !r7_parse_u64(argv[++i], &explicit_bytes)) {
                r7_usage(argv[0]);
                return 2;
            }
            has_explicit_bytes = 1;
        } else if (strcmp(argv[i], "--repeats") == 0) {
            if (i + 1 >= argc ||
                !r7_parse_u64(argv[++i], &repeats) ||
                repeats == 0u ||
                repeats > UINT64_C(100)) {
                r7_usage(argv[0]);
                return 2;
            }
            repeats_explicit = 1;
        } else {
            r7_usage(argv[0]);
            return 2;
        }
    }

    if (!has_explicit_bytes &&
        strcmp(profile, "local") == 0 &&
        !repeats_explicit) {
        repeats = UINT64_C(5);
    }

    if (has_explicit_bytes) {
        if (explicit_bytes < R7_MIN_BYTES ||
            explicit_bytes > R7_MAX_BYTES ||
            !r7_is_power_of_two_u64(explicit_bytes)) {
            fprintf(stderr, "invalid R7 working-set size\n");
            return 2;
        }

        for (repeat = 0u; repeat < repeats; ++repeat) {
            if (!r7_measure_size(explicit_bytes, repeat)) {
                fprintf(stderr, "R7 study execution failed\n");
                return 1;
            }
        }
    } else if (strcmp(profile, "smoke") == 0) {
        for (repeat = 0u; repeat < repeats; ++repeat) {
            if (!r7_measure_size(UINT64_C(32768), repeat)) {
                fprintf(stderr, "R7 study execution failed\n");
                return 1;
            }
        }
    } else {
        size_t lane;

        for (lane = 0u;
             lane < sizeof(local_sizes) / sizeof(local_sizes[0]);
             ++lane) {
            for (repeat = 0u; repeat < repeats; ++repeat) {
                if (!r7_measure_size(local_sizes[lane], repeat)) {
                    fprintf(stderr, "R7 study execution failed\n");
                    return 1;
                }
            }
        }
    }

    if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "R7 study output failure\n");
        return 1;
    }

    (void)r7_sink;
    return 0;
}
