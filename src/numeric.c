/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/numeric.h"

#include <stddef.h>
#include <stdint.h>

static int rune_rounding_valid(rune_rounding rounding)
{
    return rounding == RUNE_ROUND_TOWARD_ZERO ||
        rounding == RUNE_ROUND_FLOOR ||
        rounding == RUNE_ROUND_CEIL ||
        rounding == RUNE_ROUND_NEAREST_EVEN;
}

static uint64_t rune_i64_magnitude(int64_t value)
{
    if (value >= 0) {
        return (uint64_t)value;
    }

    return (uint64_t)(-(value + (int64_t)1)) + (uint64_t)1u;
}

static rune_status rune_i32_from_i64(int64_t value, int32_t *out)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (value < (int64_t)INT32_MIN || value > (int64_t)INT32_MAX) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = (int32_t)value;
    return RUNE_OK;
}

rune_status rune_u64_add_checked(
    uint64_t left,
    uint64_t right,
    uint64_t *out
)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (left > UINT64_MAX - right) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = left + right;
    return RUNE_OK;
}

rune_status rune_u64_sub_checked(
    uint64_t left,
    uint64_t right,
    uint64_t *out
)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (left < right) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = left - right;
    return RUNE_OK;
}

rune_status rune_u64_mul_checked(
    uint64_t left,
    uint64_t right,
    uint64_t *out
)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (right != 0u && left > UINT64_MAX / right) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = left * right;
    return RUNE_OK;
}

rune_status rune_i64_add_checked(
    int64_t left,
    int64_t right,
    int64_t *out
)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = left + right;
    return RUNE_OK;
}

rune_status rune_i64_sub_checked(
    int64_t left,
    int64_t right,
    int64_t *out
)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if ((right > 0 && left < INT64_MIN + right) ||
        (right < 0 && left > INT64_MAX + right)) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = left - right;
    return RUNE_OK;
}

rune_status rune_i64_mul_checked(
    int64_t left,
    int64_t right,
    int64_t *out
)
{
    uint64_t left_magnitude;
    uint64_t right_magnitude;
    uint64_t limit;
    uint64_t product;
    int negative;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    left_magnitude = rune_i64_magnitude(left);
    right_magnitude = rune_i64_magnitude(right);
    negative = (left < 0) != (right < 0);

    limit = (uint64_t)INT64_MAX;
    if (negative) {
        limit += (uint64_t)1u;
    }

    if (right_magnitude != 0u &&
        left_magnitude > limit / right_magnitude) {
        return RUNE_ERR_OVERFLOW;
    }

    product = left_magnitude * right_magnitude;

    if (!negative) {
        *out = (int64_t)product;
        return RUNE_OK;
    }

    if (product == (uint64_t)INT64_MAX + (uint64_t)1u) {
        *out = INT64_MIN;
        return RUNE_OK;
    }

    *out = -(int64_t)product;
    return RUNE_OK;
}

rune_status rune_u64_shl_checked(
    uint64_t value,
    uint32_t shift,
    uint64_t *out
)
{
    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (shift >= 64u) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (shift != 0u && value > (UINT64_MAX >> shift)) {
        return RUNE_ERR_OVERFLOW;
    }

    *out = value << shift;
    return RUNE_OK;
}

rune_status rune_i64_div_round(
    int64_t numerator,
    int64_t denominator,
    rune_rounding rounding,
    int64_t *out
)
{
    int64_t quotient;
    int64_t remainder;
    uint64_t remainder_magnitude;
    uint64_t denominator_magnitude;
    uint64_t complement;
    int negative;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    if (!rune_rounding_valid(rounding)) {
        return RUNE_ERR_INVALID_ARGUMENT;
    }

    if (denominator == 0) {
        return RUNE_ERR_DIVIDE_BY_ZERO;
    }

    if (numerator == INT64_MIN && denominator == (int64_t)-1) {
        return RUNE_ERR_OVERFLOW;
    }

    quotient = numerator / denominator;
    remainder = numerator % denominator;

    if (remainder == 0 || rounding == RUNE_ROUND_TOWARD_ZERO) {
        *out = quotient;
        return RUNE_OK;
    }

    negative = (numerator < 0) != (denominator < 0);

    if (rounding == RUNE_ROUND_FLOOR) {
        *out = negative ? quotient - (int64_t)1 : quotient;
        return RUNE_OK;
    }

    if (rounding == RUNE_ROUND_CEIL) {
        *out = negative ? quotient : quotient + (int64_t)1;
        return RUNE_OK;
    }

    remainder_magnitude = rune_i64_magnitude(remainder);
    denominator_magnitude = rune_i64_magnitude(denominator);
    complement = denominator_magnitude - remainder_magnitude;

    if (remainder_magnitude < complement) {
        *out = quotient;
        return RUNE_OK;
    }

    if (remainder_magnitude == complement &&
        (quotient % (int64_t)2) == 0) {
        *out = quotient;
        return RUNE_OK;
    }

    *out = negative ?
        quotient - (int64_t)1 :
        quotient + (int64_t)1;
    return RUNE_OK;
}

rune_status rune_q16_16_from_i32(
    int32_t value,
    rune_q16_16 *out
)
{
    int64_t scaled;
    int32_t raw;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    scaled = (int64_t)value * (int64_t)RUNE_Q16_16_SCALE;
    status = rune_i32_from_i64(scaled, &raw);
    if (status != RUNE_OK) {
        return status;
    }

    out->raw = raw;
    return RUNE_OK;
}

rune_status rune_q16_16_from_ratio(
    int32_t numerator,
    int32_t denominator,
    rune_rounding rounding,
    rune_q16_16 *out
)
{
    int64_t scaled_numerator;
    int64_t quotient;
    int32_t raw;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    scaled_numerator =
        (int64_t)numerator * (int64_t)RUNE_Q16_16_SCALE;

    status = rune_i64_div_round(
        scaled_numerator,
        (int64_t)denominator,
        rounding,
        &quotient
    );
    if (status != RUNE_OK) {
        return status;
    }

    status = rune_i32_from_i64(quotient, &raw);
    if (status != RUNE_OK) {
        return status;
    }

    out->raw = raw;
    return RUNE_OK;
}

rune_status rune_q16_16_to_i32(
    rune_q16_16 value,
    rune_rounding rounding,
    int32_t *out
)
{
    int64_t converted;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    status = rune_i64_div_round(
        (int64_t)value.raw,
        (int64_t)RUNE_Q16_16_SCALE,
        rounding,
        &converted
    );
    if (status != RUNE_OK) {
        return status;
    }

    return rune_i32_from_i64(converted, out);
}

rune_status rune_q16_16_add(
    rune_q16_16 left,
    rune_q16_16 right,
    rune_q16_16 *out
)
{
    int64_t sum;
    int32_t raw;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    sum = (int64_t)left.raw + (int64_t)right.raw;
    status = rune_i32_from_i64(sum, &raw);
    if (status != RUNE_OK) {
        return status;
    }

    out->raw = raw;
    return RUNE_OK;
}

rune_status rune_q16_16_sub(
    rune_q16_16 left,
    rune_q16_16 right,
    rune_q16_16 *out
)
{
    int64_t difference;
    int32_t raw;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    difference = (int64_t)left.raw - (int64_t)right.raw;
    status = rune_i32_from_i64(difference, &raw);
    if (status != RUNE_OK) {
        return status;
    }

    out->raw = raw;
    return RUNE_OK;
}

rune_status rune_q16_16_mul(
    rune_q16_16 left,
    rune_q16_16 right,
    rune_rounding rounding,
    rune_q16_16 *out
)
{
    int64_t product;
    int64_t scaled;
    int32_t raw;
    rune_status status;

    if (out == NULL) {
        return RUNE_ERR_NULL_ARGUMENT;
    }

    product = (int64_t)left.raw * (int64_t)right.raw;

    status = rune_i64_div_round(
        product,
        (int64_t)RUNE_Q16_16_SCALE,
        rounding,
        &scaled
    );
    if (status != RUNE_OK) {
        return status;
    }

    status = rune_i32_from_i64(scaled, &raw);
    if (status != RUNE_OK) {
        return status;
    }

    out->raw = raw;
    return RUNE_OK;
}
