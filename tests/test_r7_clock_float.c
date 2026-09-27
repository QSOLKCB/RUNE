/* SPDX-License-Identifier: MPL-2.0 */
#define main r7_embedded_study_main
#include "../study/r7_memory_wall.c"
#undef main

#include <float.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint64_t out;

    out = UINT64_C(123);
    if (r7_ticks_between((clock_t)0.0, (clock_t)DBL_MAX, &out)) {
        fputs("R7 floating clock regression: floating clock_t was accepted\n", stderr);
        return 1;
    }
    if (out != UINT64_C(123)) {
        fputs("R7 floating clock regression: rejected interval modified output\n", stderr);
        return 1;
    }

    puts("R7 floating clock regression: OK");
    return 0;
}
