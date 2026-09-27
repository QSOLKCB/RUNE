# R7 — CPU Memory-Wall Study

R7 is the first RUNE phase that records timing observations.

It does **not** turn a timing number into a universal performance claim.

The study uses the frozen R4 questions to ask when representation, locality,
resident state, materialization, and recomputation change observed execution
cost on one declared environment.

## Scope

The reference study executable is:

~~~text
study/r7_memory_wall.c
~~~

It covers:

1. sequential versus strided access;
2. materialized versus fused reduction;
3. AoS versus hot-field SoA;
4. microtile sweep;
5. retain versus regenerate;
6. lookup versus integer recomputation;
7. arena peak-live versus cumulative allocation traffic;
8. huge logical domain versus resident metadata.

The study is tooling. R7 makes **no runtime ABI changes**.

## Performance gate

R7 has two distinct states:

~~~text
raw observation capture
!=
reviewed performance claim
~~~

This PR permits raw observations to be produced.

Performance claims remain prohibited until a local evidence bundle is committed,
reviewed, and accompanied by an interpretation that satisfies RUNE-INV-014.

GitHub-hosted CI timings are never promoted as R7 performance evidence.

## Timer

The portable reference timer is ISO C99:

~~~text
clock()
~~~

The receipt records raw process-CPU-time ticks and CLOCKS_PER_SEC.

R7 does not call these values wall-clock nanoseconds.

If clock() is unavailable or a tick interval cannot be represented, the study
fails explicitly.

## Lifecycle accounting

Every timed observation records:

~~~text
setup_ticks
execute_ticks
teardown_ticks
total_ticks
~~~

The study therefore preserves setup/allocation and teardown costs alongside the
execution body.

Kernel-only timing is not sufficient evidence for promotion.

## Observation format

The executable emits numeric ASCII TSV with 17 columns:

~~~text
comparison_id
variant_id
working_set_bytes
repeat_index
logical_items
resident_bytes
peak_live_bytes
cumulative_traffic_bytes
model_bytes_read
model_bytes_written
result_u64
setup_ticks
execute_ticks
teardown_ticks
total_ticks
clock_ticks_per_second
lifecycle_complete
~~~

Timing fields are observations and are excluded from correctness identity.

## Comparison IDs

### 1 — Locality

~~~text
variant 1 = sequential scan
variant 2 = odd-stride full-cycle scan
~~~

Both traverse the same uint32 values and must produce the same result.

### 2 — Materialization

~~~text
variant 1 = transform -> temporary array -> reduce
variant 2 = fused transform + reduce
~~~

Both must produce the same result.

### 3 — Layout

~~~text
variant 1 = six-field AoS logical record
variant 2 = three dense hot-field arrays
~~~

The AoS representation is six consecutive uint32 lanes, avoiding host structure
padding. Both consume the same three hot fields and must produce the same result.

### 4 — Microtile

variant_id is the tile size in elements:

~~~text
32 64 128 256 512 1024 2048 4096
~~~

Every executed tile size must produce one exact result identity.

Each microtile variant allocates **only its selected tile extent** after the
total timer begins and releases it before teardown ends. The allocation is part
of setup timing, release is part of teardown timing, and resident/peak bytes
therefore describe the actual selected tile rather than a hidden fixed 4096-item
automatic buffer.

### 5 — Retain versus regenerate

~~~text
variant 1 = retained procedural uint32 array
variant 2 = deterministic regeneration from (seed, index)
~~~

Both execute the same logical passes and must produce the same result.

### 6 — Lookup versus recompute

~~~text
variant 1 = 4096-entry retained lookup
variant 2 = recompute the exact table value from (seed, index)
~~~

Both must produce the same result.

### 7 — Peak-live versus cumulative traffic

Uses the R2 arena/reset workload and records:

- fixed resident arena bytes;
- high-water bytes;
- cumulative consumed bytes across reuse;
- payload read/write accounting.

This row is evidence about **resident versus cumulative state accounting**. It
does not by itself establish a causal performance advantage.

### 8 — Logical domain versus resident state

Uses the R4 C15 shape:

~~~text
logical_items = 2^40
resident metadata = 48 bytes
64 windows x 64 generated values
~~~

The row demonstrates the scale relationship without full materialization.

## Working-set profiles

The local ladder is:

~~~text
4 KiB
32 KiB
256 KiB
1 MiB
4 MiB
16 MiB
64 MiB
~~~

The default local evidence run uses five repeats.

The harness normalizes small cases toward roughly 64 MiB of logical work per
sample so very small working sets are not represented by a single tiny loop.

Selected study storage is accessed through C99 `volatile` views where needed to
prevent an optimizing compiler from legally collapsing one declared benchmark
variant into another (for example, eliminating the materialized temporary or
the microtile). These volatile accesses are **study mechanics**, not RUNE
runtime semantics or an optimization recommendation.

## Local evidence bundle

Run:

~~~sh
./scripts/r7-run-local.sh
~~~

or:

~~~sh
RUNE_R7_REPEATS=5 CC=cc ./scripts/r7-run-local.sh evidence/r7/my-host
~~~

The script creates an **immutable new destination** and refuses to overwrite an
existing bundle. It samples Git status before creating that destination and
counts tracked, staged, and untracked files when recording dirty state.

Every bundle declares:

~~~text
evidence_class=raw-local-execution-observation
~~~

The script records:

- exact Git source revision;
- dirty/clean working-tree state including untracked files;
- resolved single-executable compiler path and successful version output;
- both CPPFLAGS and CFLAGS, and binds those exact values into the build command;
- uname platform context;
- visible processor count when available;
- CPU model when available;
- a **required memory profile**, using /proc/meminfo, sysctl, or getconf;
- timer method;
- exact commands;
- raw observations;
- SHA-256 integrity file.

A compound CC such as `ccache gcc` is rejected for evidence capture; wrappers
or flags must be represented explicitly rather than hidden inside CC. If the
compiler cannot be resolved/identified or memory context cannot be captured, the
bundle fails closed.

These fields satisfy the evidence-dimension requirement only when the bundle is
actually captured on the host. A template or planned command is not execution
evidence.

## CI smoke

~~~sh
make r7-study-smoke
~~~

CI uses one 32 KiB repeat to check:

- strict C99 compilation;
- all pairwise result identities;
- all eight tile identities;
- numeric TSV schema;
- expected observation count.

The timing values from this CI job are discarded as performance evidence.

## Interpreting results

R7 may later support environment-scoped statements such as:

> on evidence bundle X, variant A had lower median total ticks than variant B
> for working-set sizes Y under the declared toolchain and timer.

R7 may **not** infer from timing alone:

- cache capacity;
- cache misses;
- memory bandwidth saturation;
- NUMA placement;
- DRAM latency;
- CPU microarchitectural cause.

Hardware counters may strengthen a later causal interpretation.

Negative and null results must be retained.

## Phase completion

This implementation PR establishes the study machinery.

R7 is complete only after:

1. a local evidence bundle is captured;
2. raw observations are integrity-bound;
3. correctness parity is preserved;
4. an interpretation includes negative/null results;
5. any performance statement is environment-scoped and reviewed.

Until then, runtime performance claims remain prohibited.
