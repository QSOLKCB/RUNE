# R3 — Integer Numeric Core

R3 adds only the integer arithmetic needed to prove RUNE can express bounded fractional semantics without floating point.

It is intentionally not a universal mathematics library.

## Public surface

~~~text
include/rune/numeric.h
src/numeric.c
~~~

R3 provides:

- checked uint64 add/subtract/multiply;
- checked int64 add/subtract/multiply;
- checked uint64 left shift;
- signed integer division with an explicit rounding mode;
- a signed Q16.16 proof type;
- Q16.16 integer/ratio conversion;
- Q16.16 add/subtract/multiply.

## Failure semantics

Arithmetic never intentionally relies on signed overflow or silent unsigned wrap.

~~~text
overflow
underflow
result outside destination representation
    -> RUNE_ERR_OVERFLOW

division by zero
    -> RUNE_ERR_DIVIDE_BY_ZERO

invalid shift or rounding mode
    -> RUNE_ERR_INVALID_ARGUMENT
~~~

Output objects are written only after an operation has validated successfully.

## Rounding

Every public operation that discards fractional information requires a declared rounding mode:

~~~text
RUNE_ROUND_TOWARD_ZERO
RUNE_ROUND_FLOOR
RUNE_ROUND_CEIL
RUNE_ROUND_NEAREST_EVEN
~~~

Nearest-even resolves an exact half-way case toward the even integer.

Examples:

~~~text
 5 / 2 -> 2
 7 / 2 -> 4
-5 / 2 -> -2
-7 / 2 -> -4
~~~

under RUNE_ROUND_NEAREST_EVEN.

Rounding is part of result identity.

## Checked integer arithmetic

The signed multiplication path does not evaluate an overflowing signed product in order to detect overflow.

It converts operands to safe unsigned magnitudes, checks the representable result magnitude, and only then constructs the signed result. INT64_MIN is handled explicitly.

The unsigned left-shift primitive rejects:

- shifts of 64 or more bits;
- values whose non-zero bits would be shifted out.

## Q16.16 proof type

~~~text
typedef struct rune_q16_16 {
    int32_t raw;
} rune_q16_16;
~~~

The scale is exactly 65536 (2^16).

The representable interval is:

~~~text
-32768
through
32767 + 65535/65536
~~~

Q16.16 is **not** declared to be RUNE's universal fixed-point representation.

It exists because it demonstrates several required properties with only standard C99 integer types:

- exact representation identity;
- declared scale;
- explicit rounding;
- bounded conversion;
- multiplication through a guaranteed wider intermediate.

Q16.16 multiplication first forms:

~~~text
int32 raw * int32 raw -> int64 product
~~~

which is representable for every pair of int32 inputs. The product is then divided by 65536 with the caller-selected rounding mode and checked back into int32.

No compiler-specific 128-bit type is required for R3 correctness.

## Ratio conversion

rune_q16_16_from_ratio() computes:

~~~text
numerator * 65536 / denominator
~~~

using int64 intermediate arithmetic and an explicit rounding mode.

Division by zero fails explicitly. A mathematically valid ratio whose scaled result does not fit Q16.16 fails with RUNE_ERR_OVERFLOW.

## Deliberate exclusions

R3 does not add:

- floating point;
- arbitrary precision;
- decimal arithmetic;
- trigonometry;
- transcendentals;
- universal vector/matrix types;
- saturation semantics without a demonstrated workload;
- generic expression templates;
- SIMD;
- threading;
- performance claims.

Integer interpolation or additional fixed-point formats must be earned by a real later workload.

## Build and test

~~~sh
make test
make clean test CC=clang
~~~

R1, R2, and R3 deterministic correctness suites run together.

R3 makes no runtime performance claim.
