/* SPDX-License-Identifier: MPL-2.0 */
#include "rune/status.h"

const char *rune_status_name(rune_status status)
{
    switch (status) {
    case RUNE_OK:
        return "RUNE_OK";
    case RUNE_ERR_NULL_ARGUMENT:
        return "RUNE_ERR_NULL_ARGUMENT";
    case RUNE_ERR_INVALID_ARGUMENT:
        return "RUNE_ERR_INVALID_ARGUMENT";
    case RUNE_ERR_UNSUPPORTED_CAPACITY:
        return "RUNE_ERR_UNSUPPORTED_CAPACITY";
    case RUNE_ERR_OUT_OF_BOUNDS:
        return "RUNE_ERR_OUT_OF_BOUNDS";
    case RUNE_ERR_OVERFLOW:
        return "RUNE_ERR_OVERFLOW";
    case RUNE_ERR_ACCESS:
        return "RUNE_ERR_ACCESS";
    case RUNE_ERR_CAPACITY:
        return "RUNE_ERR_CAPACITY";
    case RUNE_ERR_EXHAUSTED:
        return "RUNE_ERR_EXHAUSTED";
    case RUNE_ERR_STALE_CHECKPOINT:
        return "RUNE_ERR_STALE_CHECKPOINT";
    default:
        return "RUNE_STATUS_UNKNOWN";
    }
}
