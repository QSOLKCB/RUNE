# R4 — RUNE-CORPUS-v1

R4 defines the deterministic local-memory corpus used to exercise RUNE's memory-first thesis before performance studies or advanced execution machinery are allowed to influence the architecture.

The corpus is **tooling**, not a new runtime ABI.

## Goals

RUNE-CORPUS-v1 exists to make these questions testable later:

~~~text
Does sequential locality matter for this workload?
Can materialization be removed without changing the result?
Can hot fields be represented more compactly?
Can the working set be bounded by tiles?
Can deterministic state be regenerated instead of retained?
Can a huge logical domain remain tiny in resident metadata?
~~~

R4 freezes the workloads and their correctness identities. It does **not** answer those questions with timing claims yet.

## Deterministic generator

The default seed is:

~~~text
303
~~~

Procedural uint32 inputs are derived only from:

~~~text
(seed, logical_index)
~~~

using a fixed unsigned-64 mixing transform. Unsigned wrap is defined by C99, so generation does not depend on heap addresses, wall-clock time, thread scheduling, floating point, or platform randomness.

The repository does not need to store giant input datasets.

## Profiles

### Smoke

~~~text
32 KiB
~~~

Used by CI.

### Local

~~~text
4 KiB
32 KiB
256 KiB
1 MiB
4 MiB
16 MiB
64 MiB
~~~

Intended for an ordinary local CPU machine.

### Explicit

A caller may request one power-of-two working-set size between:

~~~text
4 KiB
and
256 MiB
~~~

with:

~~~sh
./build/rune_corpus --bytes 268435456
~~~

The 256 MiB point is optional rather than part of the default local sweep.

## Workload families

### C01 — Sequential scan

Materialized uint32 input traversed in index order.

### C02 — Strided scan

The same elements traversed through an odd full-cycle stride over a power-of-two domain.

C01 and C02 must produce the same exact reduction.

### C03 — Deterministic permutation

The same elements traversed through an affine permutation whose multiplier is odd.

C01–C03 must produce the same exact reduction.

### C04 — Offset chase

Each node contains a uint32 next-index and payload. An odd-stride cycle is followed for exactly the logical item count.

The payload reduction must equal C01.

This exercises compact explicit offset/index relationships rather than native pointer identity.

### C05 — Materialize -> reduce

~~~text
input
  -> transform
  -> temporary uint32 array
  -> reduce
~~~

### C06 — Fused reduce

~~~text
input
  -> transform + reduce
  -> result
~~~

C05 and C06 must produce exactly the same result.

### C07 — AoS hot traversal

A six-field record stores three hot and three cold uint32 fields.

The reduction consumes only the hot fields.

### C08 — SoA hot fields

The same hot values are held in three dense uint32 arrays.

C07 and C08 must produce exactly the same result.

### C09 — Microtile sweep

The same procedural transform/reduction is executed through these tile sizes when they fit:

~~~text
32
64
128
256
512
1024
2048
4096 elements
~~~

All checked tile sizes must produce one exact result identity.

### C10 — Caller-owned ring workload

A fixed 256-entry corpus-local uint32 ring exercises fill, partial drain, wrap, and final drain.

This is **not** the R5 runtime ring API. It is only a workload witness.

The consumed result must equal the procedural source reduction.

### C11 — Arena/reset

Uses the R2 arena over a bounded backing span.

Multiple fill/consume rounds reset the arena between rounds while preserving high-water and cumulative accounting.

The workload verifies that cumulative consumed bytes exceed peak-live bytes when storage is reused.

### C12 — Fixed-point reconstruction

A complete 4096-entry Q16.16 raw table is compared with exact reconstruction from 1024 base entries plus a fixed raw step.

Both representations must hash to exactly the same result.

This is an exact-representation test, not a lossy interpolation claim.

### C13 — Retain versus regenerate

Two receipts are emitted:

~~~text
C13-retain
C13-regenerate
~~~

Both perform four logical passes and must produce exactly the same result.

The regenerate path derives each value from (seed, index) instead of reading retained input.

No performance conclusion is made in R4.

### C14 — RIVET-like byte stream

A deterministic pseudo-document byte sequence contains:

- angle brackets;
- ASCII text;
- whitespace;
- digits;
- high-bit UTF-8-like bytes.

It is **not** an HTML parser.

Direct classification is compared with chunked execution at:

~~~text
7
31
257 bytes
~~~

All chunk sizes must produce the same exact classification identity.

### C15 — Huge logical domain

~~~text
logical_items = 2^40
~~~

R4 does not iterate or materialize 2^40 items.

It deterministically selects 64 windows of 64 items and represents the huge domain with constant-size metadata.

This freezes the invariant:

~~~text
LOGICAL SCALE != RESIDENT SCALE
~~~

## Receipts

Each workload emits one JSON Lines receipt with:

~~~text
contract
profile
working_set_bytes
workload
seed
logical_items
resident_bytes
scratch_bytes
model_bytes_read
model_bytes_written
result_u64
variants_checked
~~~

C13 intentionally emits two receipts, so one working-set run emits 16 workload receipts for the 15 workload families, followed by one summary receipt.

The summary contains a deterministic fingerprint over the workload receipts.

## Modelled byte traffic

R4 fields named:

~~~text
model_bytes_read
model_bytes_written
~~~

describe the declared algorithmic byte extents of one canonical workload path.

They are **not** measured DRAM traffic, cache-line traffic, cache misses, NUMA evidence, or hardware-counter observations.

Those causal/performance questions belong to later evidence phases.

## No timing in R4

R4 receipts contain no:

- elapsed time;
- CPU cycles;
- throughput;
- speedup;
- benchmark ranking.

~~~text
RESULT IDENTITY != TIMING OBSERVATION
~~~

Timing is intentionally deferred to the later CPU memory-wall study.

## Harness allocation

Some corpus workloads materialize comparison datasets using bounded libc malloc/free in the **corpus executable**.

That does not create a mandatory heap requirement for the RUNE runtime.

The harness allocation is bounded by the selected profile and is not exported as runtime API.

## Build and run

~~~sh
make test
make corpus-smoke
./build/rune_corpus --profile local
./build/rune_corpus --bytes 1048576 --seed 303
~~~

CI runs the smoke corpus with GCC and Clang.

R4 makes no runtime performance claim.
