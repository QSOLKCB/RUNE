# R1 — Region Core

R1 is RUNE's first executable reference slice.

Its purpose is intentionally narrow: prove that useful memory operations can be expressed in ISO C99 through caller-owned bounded storage, explicit semantic offsets, validated spans, and explicit failure results without requiring a heap or application semantics.

## Public surface

Headers:

~~~text
include/rune/status.h
include/rune/region.h
~~~

Implementation:

~~~text
src/status.c
src/region.c
~~~

The R1 API provides:

- explicit status/error values;
- borrowed caller-owned region attachment;
- read/write access capabilities;
- portable uint64_t capacity, offset, and length semantics;
- checked span construction;
- bounded child-span slicing;
- overlap-safe span movement;
- fill and clear operations.

## Ownership and lifetime

R1 never allocates or frees the backing storage.

~~~text
caller owns storage
      |
      v
rune_region borrows it
      |
      v
rune_span describes a checked bounded view
~~~

The caller must keep the backing object alive for every operation that uses the region or a span derived from it.

A zero-capacity region may use a null storage pointer. A non-zero capacity may not.

C cannot discover the true size of an arbitrary caller-provided object. The declared region capacity is therefore a caller contract. RUNE validates all offsets and extents against that declaration and never intentionally accesses beyond it.

## Portable semantic widths

R1 uses uint64_t for region capacities and span offsets/lengths.

That does **not** imply every host can address UINT64_MAX bytes.

Before attachment succeeds, RUNE verifies that the declared capacity can be represented by the host size_t. Pointer arithmetic occurs only after that host-fit gate and after span bounds have been checked.

~~~text
portable semantic extent
        !=
host pointer width
~~~

## Span rules

A span has:

- one attached region;
- an offset;
- a length;
- explicit access rights.

The interval is [offset, offset + length).

A zero-length span at exactly region.capacity is valid. A non-empty span or zero-length span beyond capacity is rejected.

Offset-plus-length overflow is rejected before bounds comparison.

Child spans are bounded by their parent span, not merely by the whole region.

## Movement

rune_span_move() moves exactly the source span length.

The destination may be larger but may not be smaller.

Movement requires:

~~~text
source access      = readable
destination access = writable
extent             = source.length
~~~

memmove semantics are used so overlapping views are valid.

All validation completes before bytes are modified. Capacity or access failure therefore leaves storage unchanged.

## Fill and clear

rune_span_fill() and rune_span_clear() require write access and act only on the validated destination span.

Zero-length operations succeed without dereferencing a null backing pointer.

## Out of scope

R1 does not add:

- arenas;
- scratch allocation;
- heap ownership;
- fixed-point arithmetic;
- queues or rings;
- execution descriptors;
- threads;
- SIMD;
- CPU calibration;
- performance benchmarks.

Those belong to later roadmap phases.

## Build and test

~~~sh
make test
make clean test CC=clang
~~~

The build uses strict C99 diagnostics:

~~~text
-std=c99
-Wall
-Wextra
-Werror
-pedantic
~~~

CI runs the same deterministic correctness tests with GCC and Clang.

R1 makes no runtime performance claim.
