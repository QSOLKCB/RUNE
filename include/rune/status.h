/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RUNE_STATUS_H
#define RUNE_STATUS_H

typedef enum rune_status {
    RUNE_OK = 0,
    RUNE_ERR_NULL_ARGUMENT = 1,
    RUNE_ERR_INVALID_ARGUMENT = 2,
    RUNE_ERR_UNSUPPORTED_CAPACITY = 3,
    RUNE_ERR_OUT_OF_BOUNDS = 4,
    RUNE_ERR_OVERFLOW = 5,
    RUNE_ERR_ACCESS = 6,
    RUNE_ERR_CAPACITY = 7,
    RUNE_ERR_EXHAUSTED = 8,
    RUNE_ERR_STALE_CHECKPOINT = 9,
    RUNE_ERR_DIVIDE_BY_ZERO = 10,
    RUNE_ERR_UNSUPPORTED_OPERATION = 11
} rune_status;

const char *rune_status_name(rune_status status);

#endif
