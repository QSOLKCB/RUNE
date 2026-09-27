/* SPDX-License-Identifier: MPL-2.0 */
#define main r7_embedded_study_main
#include "../study/r7_memory_wall.c"
#undef main

#include <stdint.h>
#include <stdio.h>

static int require_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "R7 clock regression: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void)
{
    uint64_t out;

    out = UINT64_C(99);
    if (!require_true(
            r7_ticks_between((clock_t)5, (clock_t)12, &out) &&
            out == UINT64_C(7),
            "normal positive delta failed")) {
        return 1;
    }

    if (!require_true(
            !r7_ticks_between((clock_t)12, (clock_t)5, &out),
            "decreasing clock interval was accepted")) {
        return 1;
    }

    if (!require_true(
            !r7_ticks_between((clock_t)0, (clock_t)0, NULL),
            "NULL output pointer was accepted")) {
        return 1;
    }

    if ((clock_t)-1 < (clock_t)0 &&
        sizeof(clock_t) >= sizeof(int64_t)) {
        clock_t extreme_start;

        extreme_start = (clock_t)INT64_MIN;
        if ((int64_t)extreme_start == INT64_MIN) {
            out = UINT64_C(123);
            if (!require_true(
                    !r7_ticks_between(extreme_start, (clock_t)0, &out),
                    "unrepresentable signed clock delta was accepted")) {
                return 1;
            }
        }
    }

    puts("R7 clock regressions: OK");
    return 0;
}
