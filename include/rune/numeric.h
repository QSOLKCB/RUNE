/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RUNE_NUMERIC_H
#define RUNE_NUMERIC_H

#include <stdint.h>

#include "rune/status.h"

typedef enum rune_rounding {
    RUNE_ROUND_TOWARD_ZERO = 0,
    RUNE_ROUND_FLOOR = 1,
    RUNE_ROUND_CEIL = 2,
    RUNE_ROUND_NEAREST_EVEN = 3
} rune_rounding;

#define RUNE_Q16_16_SCALE ((int32_t)65536)

typedef struct rune_q16_16 {
    int32_t raw;
} rune_q16_16;

rune_status rune_u64_add_checked(
    uint64_t left,
    uint64_t right,
    uint64_t *out
);

rune_status rune_u64_sub_checked(
    uint64_t left,
    uint64_t right,
    uint64_t *out
);

rune_status rune_u64_mul_checked(
    uint64_t left,
    uint64_t right,
    uint64_t *out
);

rune_status rune_i64_add_checked(
    int64_t left,
    int64_t right,
    int64_t *out
);

rune_status rune_i64_sub_checked(
    int64_t left,
    int64_t right,
    int64_t *out
);

rune_status rune_i64_mul_checked(
    int64_t left,
    int64_t right,
    int64_t *out
);

rune_status rune_u64_shl_checked(
    uint64_t value,
    uint32_t shift,
    uint64_t *out
);

rune_status rune_i64_div_round(
    int64_t numerator,
    int64_t denominator,
    rune_rounding rounding,
    int64_t *out
);

rune_status rune_q16_16_from_i32(
    int32_t value,
    rune_q16_16 *out
);

rune_status rune_q16_16_from_ratio(
    int32_t numerator,
    int32_t denominator,
    rune_rounding rounding,
    rune_q16_16 *out
);

rune_status rune_q16_16_to_i32(
    rune_q16_16 value,
    rune_rounding rounding,
    int32_t *out
);

rune_status rune_q16_16_add(
    rune_q16_16 left,
    rune_q16_16 right,
    rune_q16_16 *out
);

rune_status rune_q16_16_sub(
    rune_q16_16 left,
    rune_q16_16 right,
    rune_q16_16 *out
);

rune_status rune_q16_16_mul(
    rune_q16_16 left,
    rune_q16_16 right,
    rune_rounding rounding,
    rune_q16_16 *out
);

#endif
