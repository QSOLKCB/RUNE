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

All eight declared tile sizes execute for every valid R7 working-set size,
including the 4 KiB profile. A tile may therefore be larger than the logical
input: it still allocates the full selected tile extent but processes only the
available logical items. Every tile size must produce the same exact result
identity.

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

The local profile itself defaults to five repeats. Therefore both:

~~~sh
./build/rune_r7_study --profile local
~~~

and the evidence wrapper use five repeats unless `--repeats` (or the wrapper's
RUNE_R7_REPEATS input) explicitly overrides that value.

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
existing bundle. Destinations may not resolve inside, or lexically route
through, the repository `build/` tree, and parent-directory traversal is
rejected. The script clears Git repository-routing overrides, resolves Git only from the
fixed provenance path `/usr/bin:/bin:/usr/sbin:/sbin`, records that absolute
Git executable and version, sets `GIT_NO_REPLACE_OBJECTS=1`, rejects active
`refs/replace`, and anchors all provenance reads to `repo_root` with
`git -C`. Evidence builds use that same fixed system tool path; the build-tool
search path is not caller-overridable. Before creating the
destination it rejects Git index flags that can hide tracked changes
(`assume-unchanged` and `skip-worktree`), rejects ignored untracked files
under the compiler-input trees `src/`, `include/`, and `study/`, and
compares the raw bytes of every tracked Makefile/source/include/study input
against the recorded revision using `git hash-object --no-filters`. It then
materializes **Makefile, src/, include/, and study/** directly from
`git archive source_revision` into a per-capture source snapshot. The extracted
bytes are checked against the recorded revision and made read-only before Make
runs. Make executes inside that snapshot, so the mutable checkout is not a
compiler input. Worktree and snapshot identity are checked again after the build
and after measurement. The bundle directory is not created until the post-build
checks pass. **Any dirty or divergent checkout/snapshot is rejected.** R7 local
evidence
therefore binds directly to the recorded `source_revision`; a bundle may not
claim a commit while actually building uncommitted source content. Files marked
`assume-unchanged` or `skip-worktree` are rejected because those index flags
can suppress ordinary dirty-state reporting.

Every bundle declares:

~~~text
evidence_class=raw-local-execution-observation
~~~

The script records:

- exact Git source revision, with Git routing overrides cleared, replacement
  objects disabled/rejected, provenance commands explicitly rooted at the
  repository, and raw compiler-input bytes verified against revision blobs
  without clean filters;
- a verified clean working tree, including absence of untracked files and
  ignored untracked files under `src/`, `include/`, and `study/`;
- compiler, archiver, and build-driver executables resolved from the fixed
  provenance path, with successful identity/version output; arbitrary external
  compiler/archiver wrappers are not accepted for evidence capture;
- both CPPFLAGS and CFLAGS, recorded with `printf` so backslashes and other
  shell-text content are preserved exactly, binding those values plus CC and AR
  into the build command;
- explicit sanitation of ambient compiler search-path variables
  (`CPATH`, `C_INCLUDE_PATH`, `CPLUS_INCLUDE_PATH`,
  `OBJC_INCLUDE_PATH`, `COMPILER_PATH`, `LIBRARY_PATH`,
  `GCC_EXEC_PREFIX`);
- explicit sanitation of dynamic-loader injection/search variables including
  `LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_AUDIT`, the relevant
  `DYLD_*` variables, `LIBPATH`, and `SHLIB_PATH`;
- mandatory successful, nonempty `uname -a` platform identity resolved from
  the fixed provenance path and recorded by absolute executable path;
- visible processor count when available;
- CPU model when available;
- a **required memory profile**, with Linux parsing performed by `awk`
  resolved from the fixed provenance path and optional `sysctl`/`getconf`
  fallbacks resolved from that same path;
- timer method;
- exact commands;
- raw observations;
- SHA-256 integrity file produced by a SHA-256 utility resolved from the fixed
  provenance path, with the utility path/identity recorded before finalization.

A compound CC such as `ccache gcc` is rejected for evidence capture; wrappers
or flags must be represented explicitly rather than hidden inside CC. Ambient
compiler/header search-path variables are unset before compiler identity and the
evidence build; intentional include paths must be expressed in recorded
`CPPFLAGS`. Dynamic-loader injection/search variables are also cleared before
toolchain identity and the measured executable are launched, so an ambient
preload cannot replace `clock()` or other measured behavior without being
part of the recorded source/build configuration. CI executes hostile-environment
self-tests for Git routing, compiler search paths, ignored compiler inputs, and
dynamic-loader injection. If the
compiler cannot be resolved/identified or memory context cannot be captured, the
bundle fails closed.

Evidence destinations resolving inside or lexically routing through `build/`
are rejected before bundle creation, including symlink routes whose lexical
parent would be removed or replaced by build activity. Parent-directory
traversal in the requested destination is also rejected. `RUNE_R7_REPEATS`
is validated as 1..100 before the destination is reserved. Bundle creation uses
a `mkdir` executable resolved from the fixed provenance path, and the newly
created directory is verified empty before any evidence file is written.

The evidence build does **not** depend on `make clean`. Each capture receives
a unique source-snapshot directory and a fresh `BUILD_DIR`, both identified by
UTC timestamp plus shell PID, so concurrent captures remain isolated. Source
material is extracted from the recorded Git tree, verified, and made read-only.
The build clears inherited `MAKEFLAGS`, `GNUMAKEFLAGS`, `MFLAGS`,
`MAKEFILES`, and `MAKEOVERRIDES`, uses the fixed
`/usr/bin:/bin:/usr/sbin:/sbin` tool path, and invokes absolute trusted Make,
compiler, archiver, and utility paths. The entire source/build snapshot is
removed after measurement.

The sanitized invocation is recorded in `command.txt` using POSIX single-quote
escaping. The quote serializer uses `sed` resolved from the fixed provenance
path rather than ambient `PATH`, so repository paths containing apostrophes
remain replayable even in a hostile shell environment.

The build recipe is pinned explicitly with the resolved absolute Make path and
`-C "$source_snapshot" -f Makefile`. The replay commands first recreate the
same Git-tree snapshot from `source_revision`. Make-facing values remain
snapshot-relative:
the study target is `build/.../rune_r7_study` and `BUILD_DIR` is
`build/...`. The shell-facing executable path remains absolute. This avoids
Make parsing repository-path whitespace while preserving replayability from any
working directory.
The script verifies that `Makefile` is tracked and records the Git blob ID of
`source_revision:Makefile`. Ignored or globally excluded `GNUmakefile` or
lowercase `makefile` files therefore cannot override the evidence build.
`command.txt` records that explicit working directory, uses an absolute path
for the study executable, and records the exact absolute
`> .../observations.tsv` redirection used by capture. Replaying the file from
another working directory therefore recreates the observation artifact rather
than sending numeric rows to the caller's stdout. CI runs the quote serializer's built-in self-test and executes the
pinned build/run form from outside the repository. CI also reproduces
symlink-routed output paths, active Git replacement refs, clean-filter-hidden
raw source edits, and a hostile ambient `PATH` containing a fake `rm`; the
fresh-build self-test must still compile and execute a 32 KiB smoke study.
CI also launches two fresh-build self-tests concurrently and executes the same
self-test from a linked worktree whose repository path contains spaces.

The `getconf` memory fallback is valid only when both `_PHYS_PAGES` and
`PAGE_SIZE` are present, numeric, and nonzero; a one-sided memory profile is
not accepted as complete evidence.

The script fails before bundle creation if `uname -a` fails or returns empty
output, preventing an apparently complete bundle with missing platform
provenance.

These fields satisfy the evidence-dimension requirement only when the bundle is
actually captured on the host. A template or planned command is not execution
evidence.

## CI smoke

~~~sh
make r7-study-smoke
~~~

CI uses one 32 KiB repeat and an explicit 4 KiB regression run to check:

- strict C99 compilation;
- all pairwise result identities;
- all eight tile identities;
- numeric TSV schema;
- expected observation count;
- all eight microtile IDs (32 through 4096) execute at the 4 KiB profile,
  yielding 20 rows rather than silently omitting the larger tiles.

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
