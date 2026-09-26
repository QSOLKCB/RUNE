/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/numeric.h"
#include "rune/status.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures += 1; \
        } \
    } while (0)

static void check_status(
    rune_status actual,
    rune_status expected,
    const char *expr,
    int line
)
{
    if (actual != expected) {
        fprintf(
            stderr,
            "%s:%d: %s: expected %s, got %s\n",
            __FILE__,
            line,
            expr,
            rune_status_name(expected),
            rune_status_name(actual)
        );
        failures += 1;
    }
}

#define CHECK_STATUS(expr, expected) \
    check_status((expr), (expected), #expr, __LINE__)

static void test_status_name(void)
{
    CHECK(strcmp(
        rune_status_name(RUNE_ERR_DIVIDE_BY_ZERO),
        "RUNE_ERR_DIVIDE_BY_ZERO") == 0);
}

static void test_u64_checked(void)
{
    uint64_t out = 99u;

    CHECK_STATUS(rune_u64_add_checked(2u, 3u, &out), RUNE_OK);
    CHECK(out == 5u);
    CHECK_STATUS(
        rune_u64_add_checked(UINT64_MAX, 1u, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK(out == 5u);

    CHECK_STATUS(rune_u64_sub_checked(9u, 4u, &out), RUNE_OK);
    CHECK(out == 5u);
    CHECK_STATUS(rune_u64_sub_checked(4u, 9u, &out), RUNE_ERR_OVERFLOW);
    CHECK(out == 5u);

    CHECK_STATUS(rune_u64_mul_checked(7u, 6u, &out), RUNE_OK);
    CHECK(out == 42u);
    CHECK_STATUS(
        rune_u64_mul_checked(UINT64_MAX, 2u, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK(out == 42u);

    CHECK_STATUS(rune_u64_mul_checked(UINT64_MAX, 1u, &out), RUNE_OK);
    CHECK(out == UINT64_MAX);
}

static void test_i64_checked(void)
{
    int64_t out = 77;

    CHECK_STATUS(rune_i64_add_checked(2, -5, &out), RUNE_OK);
    CHECK(out == -3);
    CHECK_STATUS(
        rune_i64_add_checked(INT64_MAX, 1, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK(out == -3);
    CHECK_STATUS(
        rune_i64_add_checked(INT64_MIN, -1, &out),
        RUNE_ERR_OVERFLOW
    );

    CHECK_STATUS(rune_i64_sub_checked(-2, -5, &out), RUNE_OK);
    CHECK(out == 3);
    CHECK_STATUS(
        rune_i64_sub_checked(INT64_MIN, 1, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK_STATUS(
        rune_i64_sub_checked(INT64_MAX, -1, &out),
        RUNE_ERR_OVERFLOW
    );

    CHECK_STATUS(rune_i64_mul_checked(-7, 6, &out), RUNE_OK);
    CHECK(out == -42);
    CHECK_STATUS(rune_i64_mul_checked(INT64_MIN, 1, &out), RUNE_OK);
    CHECK(out == INT64_MIN);
    CHECK_STATUS(
        rune_i64_mul_checked(INT64_MIN, -1, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK(out == INT64_MIN);
    CHECK_STATUS(
        rune_i64_mul_checked(INT64_MAX, 2, &out),
        RUNE_ERR_OVERFLOW
    );
}

static void test_shift(void)
{
    uint64_t out = 17u;

    CHECK_STATUS(rune_u64_shl_checked(3u, 4u, &out), RUNE_OK);
    CHECK(out == 48u);
    CHECK_STATUS(
        rune_u64_shl_checked(1u, 63u, &out),
        RUNE_OK
    );
    CHECK(out == ((uint64_t)1u << 63u));
    CHECK_STATUS(
        rune_u64_shl_checked(2u, 63u, &out),
        RUNE_ERR_OVERFLOW
    );
    CHECK_STATUS(
        rune_u64_shl_checked(1u, 64u, &out),
        RUNE_ERR_INVALID_ARGUMENT
    );
}

static void check_div(
    int64_t numerator,
    int64_t denominator,
    rune_rounding rounding,
    int64_t expected
)
{
    int64_t out = INT64_C(123456789);

    CHECK_STATUS(
        rune_i64_div_round(
            numerator,
            denominator,
            rounding,
            &out
        ),
        RUNE_OK
    );
    CHECK(out == expected);
}

static void test_div_round(void)
{
    int64_t out = 55;

    check_div(7, 2, RUNE_ROUND_TOWARD_ZERO, 3);
    check_div(-7, 2, RUNE_ROUND_TOWARD_ZERO, -3);
    check_div(-7, 2, RUNE_ROUND_FLOOR, -4);
    check_div(-7, 2, RUNE_ROUND_CEIL, -3);
    check_div(7, -2, RUNE_ROUND_FLOOR, -4);
    check_div(7, -2, RUNE_ROUND_CEIL, -3);
    check_div(5, 2, RUNE_ROUND_NEAREST_EVEN, 2);
    check_div(7, 2, RUNE_ROUND_NEAREST_EVEN, 4);
    check_div(-5, 2, RUNE_ROUND_NEAREST_EVEN, -2);
    check_div(-7, 2, RUNE_ROUND_NEAREST_EVEN, -4);
    check_div(INT64_MIN, 1, RUNE_ROUND_NEAREST_EVEN, INT64_MIN);

    CHECK_STATUS(
        rune_i64_div_round(1, 0, RUNE_ROUND_TOWARD_ZERO, &out),
        RUNE_ERR_DIVIDE_BY_ZERO
    );
    CHECK(out == 55);
    CHECK_STATUS(
        rune_i64_div_round(
            INT64_MIN,
            -1,
            RUNE_ROUND_TOWARD_ZERO,
            &out
        ),
        RUNE_ERR_OVERFLOW
    );
    CHECK_STATUS(
        rune_i64_div_round(1, 2, (rune_rounding)99, &out),
        RUNE_ERR_INVALID_ARGUMENT
    );
}

static void test_q16_construction_and_conversion(void)
{
    rune_q16_16 value;
    int32_t integer = 99;

    CHECK_STATUS(rune_q16_16_from_i32(1, &value), RUNE_OK);
    CHECK(value.raw == RUNE_Q16_16_SCALE);

    CHECK_STATUS(rune_q16_16_from_i32(-32768, &value), RUNE_OK);
    CHECK(value.raw == INT32_MIN);
    CHECK_STATUS(
        rune_q16_16_from_i32(32768, &value),
        RUNE_ERR_OVERFLOW
    );

    CHECK_STATUS(
        rune_q16_16_from_ratio(
            1,
            2,
            RUNE_ROUND_NEAREST_EVEN,
            &value
        ),
        RUNE_OK
    );
    CHECK(value.raw == 32768);

    CHECK_STATUS(
        rune_q16_16_from_ratio(
            1,
            3,
            RUNE_ROUND_NEAREST_EVEN,
            &value
        ),
        RUNE_OK
    );
    CHECK(value.raw == 21845);

    CHECK_STATUS(
        rune_q16_16_from_ratio(
            1,
            0,
            RUNE_ROUND_NEAREST_EVEN,
            &value
        ),
        RUNE_ERR_DIVIDE_BY_ZERO
    );

    value.raw = 98304;
    CHECK_STATUS(
        rune_q16_16_to_i32(
            value,
            RUNE_ROUND_TOWARD_ZERO,
            &integer
        ),
        RUNE_OK
    );
    CHECK(integer == 1);
    CHECK_STATUS(
        rune_q16_16_to_i32(
            value,
            RUNE_ROUND_NEAREST_EVEN,
            &integer
        ),
        RUNE_OK
    );
    CHECK(integer == 2);

    value.raw = -98304;
    CHECK_STATUS(
        rune_q16_16_to_i32(
            value,
            RUNE_ROUND_NEAREST_EVEN,
            &integer
        ),
        RUNE_OK
    );
    CHECK(integer == -2);
}

static void test_q16_arithmetic(void)
{
    rune_q16_16 a;
    rune_q16_16 b;
    rune_q16_16 out;

    a.raw = 98304;
    b.raw = 131072;
    CHECK_STATUS(rune_q16_16_add(a, b, &out), RUNE_OK);
    CHECK(out.raw == 229376);

    CHECK_STATUS(rune_q16_16_sub(b, a, &out), RUNE_OK);
    CHECK(out.raw == 32768);

    CHECK_STATUS(
        rune_q16_16_mul(
            a,
            b,
            RUNE_ROUND_NEAREST_EVEN,
            &out
        ),
        RUNE_OK
    );
    CHECK(out.raw == 196608);

    a.raw = 1;
    b.raw = 32768;
    CHECK_STATUS(
        rune_q16_16_mul(
            a,
            b,
            RUNE_ROUND_NEAREST_EVEN,
            &out
        ),
        RUNE_OK
    );
    CHECK(out.raw == 0);
    CHECK_STATUS(
        rune_q16_16_mul(
            a,
            b,
            RUNE_ROUND_CEIL,
            &out
        ),
        RUNE_OK
    );
    CHECK(out.raw == 1);

    a.raw = INT32_MAX;
    b.raw = 1;
    out.raw = 123;
    CHECK_STATUS(rune_q16_16_add(a, b, &out), RUNE_ERR_OVERFLOW);
    CHECK(out.raw == 123);

    a.raw = INT32_MIN;
    b.raw = 1;
    CHECK_STATUS(rune_q16_16_sub(a, b, &out), RUNE_ERR_OVERFLOW);
    CHECK(out.raw == 123);

    a.raw = INT32_MAX;
    b.raw = INT32_MAX;
    CHECK_STATUS(
        rune_q16_16_mul(
            a,
            b,
            RUNE_ROUND_TOWARD_ZERO,
            &out
        ),
        RUNE_ERR_OVERFLOW
    );
    CHECK(out.raw == 123);
}

static void test_null_outputs(void)
{
    rune_q16_16 value;

    value.raw = 0;
    CHECK_STATUS(rune_u64_add_checked(1u, 2u, NULL), RUNE_ERR_NULL_ARGUMENT);
    CHECK_STATUS(rune_i64_mul_checked(1, 2, NULL), RUNE_ERR_NULL_ARGUMENT);
    CHECK_STATUS(
        rune_i64_div_round(1, 2, RUNE_ROUND_TOWARD_ZERO, NULL),
        RUNE_ERR_NULL_ARGUMENT
    );
    CHECK_STATUS(rune_q16_16_from_i32(1, NULL), RUNE_ERR_NULL_ARGUMENT);
    CHECK_STATUS(
        rune_q16_16_mul(
            value,
            value,
            RUNE_ROUND_TOWARD_ZERO,
            NULL
        ),
        RUNE_ERR_NULL_ARGUMENT
    );
}

int main(void)
{
    test_status_name();
    test_u64_checked();
    test_i64_checked();
    test_shift();
    test_div_round();
    test_q16_construction_and_conversion();
    test_q16_arithmetic();
    test_null_outputs();

    if (failures != 0) {
        fprintf(stderr, "RUNE R3 numeric tests: FAIL (%d)\n", failures);
        return 1;
    }

    puts("RUNE R3 numeric tests: PASS");
    return 0;
}
