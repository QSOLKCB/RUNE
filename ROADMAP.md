# RUNE Roadmap

The roadmap is deliberately staged so measurement and portability are earned before optimization machinery appears.

No later phase may be pulled forward merely because it is exciting.

## R0 — Constitutional foundation

**Complete — merged in PR #1.**

Goals:

- freeze the project mission and RUNE v1 invariants;
- freeze ISO C99 as the reference-language baseline;
- freeze CPU-only and integer-only public execution doctrines;
- define human-facing versus machine-readable authority surfaces;
- record donor mechanisms and adoption boundaries;
- define initial portability and evidence language;
- retain MPL-2.0 licensing;
- publish no runtime performance claims.

R0 contains no required runtime implementation.

Success criterion:

> the project can explain what RUNE is, what it is not, how agents must interpret it, and what evidence future implementation claims require.

## R1 — Region core

**Complete — merged in PR #2.**

Build the smallest useful C99 memory substrate.

Candidate scope:

- fixed-width integer vocabulary;
- explicit result/error vocabulary;
- caller-owned region attachment;
- checked offset and extent arithmetic;
- bounded read/write spans;
- overlap-safe movement;
- fill/clear operations;
- explicit capacity failure;
- deterministic tests;
- no mandatory heap.

Portable semantic offsets must not silently depend on host pointer width.

R1 makes correctness and portability claims only. Runtime performance claims remain out of scope.

Success criterion:

> useful bounded memory operations execute deterministically without dynamic allocation or application-specific semantics.

## R2 — Arena and scratch lifetime

**Complete — merged in PR #3.**

Introduce controlled transient storage only after R1 exists.

Candidate scope:

- monotonic caller-owned arena;
- alignment handling;
- bounded allocation from a declared region;
- explicit reset/checkpoint semantics;
- exhaustion result;
- high-water accounting;
- peak-live versus cumulative allocation accounting.

No general-purpose heap abstraction is required.

Success criterion:

> temporary state has an explicit maximum residency and deterministic lifetime.

## R3 — Integer numeric core

**Current phase.**

Add only arithmetic required by real RUNE workloads.

Candidate scope:

- checked integer add/subtract/multiply;
- wider intermediates where required for exactness;
- shifts and scale conversion;
- explicit rounding;
- fixed-point proof type or types earned by tests;
- integer ratio conversion;
- exact interpolation/reconstruction tests where justified.

No universal numeric framework is planned.

Success criterion:

> RUNE can express bounded fractional and scaling semantics without requiring floating point.

## R4 — RUNE-CORPUS-v1

Create a deterministic procedural local-memory corpus before adding advanced execution machinery.

Initial workload families:

- C01 sequential scan;
- C02 strided scan;
- C03 deterministic permutation;
- C04 offset chase;
- C05 materialize -> reduce;
- C06 fused consume/reduce;
- C07 AoS traversal;
- C08 bounded hot-field SoA;
- C09 microtile sweep;
- C10 caller-owned ring;
- C11 arena/reset;
- C12 fixed-point lookup/reconstruction;
- C13 retain versus regenerate;
- C14 RIVET-like byte stream;
- C15 huge logical domain with constant-size resident metadata.

Initial working-set ladder should use explicit byte sizes rather than guessed cache labels. Suggested starting points may include 4 KiB, 32 KiB, 256 KiB, 1 MiB, 4 MiB, 16 MiB, 64 MiB, 256 MiB, with larger cases optional when the host can run them comfortably.

Correctness identity must be independent of benchmark timing.

The first corpus is intentionally local-first. It should be useful on an ordinary CPU machine and must not require high-end hardware.

Success criterion:

> the project can observe representation, locality, materialisation, and recomputation crossovers without requiring a large external dataset or accelerator.

## R5 — Queues, rings and bounded streaming

Add dataflow primitives only after region/arena/numeric semantics and the first corpus exist.

Candidate scope:

- fixed-capacity FIFO;
- caller-owned ring storage;
- ring cursor state separated from storage ownership;
- wrapped two-span read/write views;
- bounded chunk processing;
- stream -> consume/reduce -> discard proof;
- logical input larger than resident working state.

Baseline remains single-threaded.

Success criterion:

> a logical stream larger than active memory can be processed under a fixed resident ceiling.

## R6 — Deterministic operation descriptors and receipts

Define only the reusable execution description demonstrated by real RUNE operations.

Candidate scope:

- operation identity;
- versioned semantic parameters;
- bounded input/output span identities;
- scratch requirement;
- status/result identity;
- compact accounting fields;
- deterministic replay vectors;
- machine-readable proof receipts.

Avoid a general task graph.

Success criterion:

> one operation can be replayed from explicit identity, parameters, bounded storage, and result contract without depending on hidden process state.

## R7 — CPU memory-wall study

Use RUNE-CORPUS-v1 to test the core thesis.

Required comparisons should include, where meaningful:

- sequential versus locality-hostile access;
- materialized versus fused reduction;
- AoS versus bounded hot-field SoA;
- tile-size sweeps;
- retain versus regenerate;
- lookup versus integer recomputation;
- peak-live versus cumulative traffic;
- logical-domain size versus resident size.

Primary questions:

~~~text
When does less resident state win?
When does more arithmetic beat more memory traffic?
When does materialisation stop paying?
Where do locality cliffs occur on this host?
~~~

Do not infer the cause of a plateau from timing alone. Hardware counters may strengthen later claims but are not required for the first local study.

Success criterion:

> RUNE has environment-scoped evidence about its memory-first design choices, including negative results.

## R8 — RIVET adapter

Only after the standalone RUNE contracts are stable:

- define a narrow RIVET-to-RUNE adapter;
- identify operations RIVET genuinely benefits from;
- preserve RIVET semantic authority;
- keep browser/document/UI concepts outside RUNE;
- compare against RIVET's reference behaviour;
- retain a path that can diagnose whether the adapter actually reduces work or memory traffic.

RUNE must earn its place beneath RIVET.

Success criterion:

> RIVET can consume RUNE without RUNE becoming RIVET's private backend or redefining RIVET semantics.

## R9 — Portability expansion

Prove the C99 baseline across materially different targets.

Candidate lanes:

- modern POSIX x86-64;
- Win32;
- 32-bit x86;
- PowerPC big-endian;
- ARM;
- m68k or other historical targets where practical;
- emulation and physical systems with explicit evidence labels.

Evidence rules:

~~~text
CROSS-COMPILE != EXECUTED TARGET
EMULATION != PHYSICAL HARDWARE
CPU FAMILY != OS FAMILY
PLAN != EXECUTION
~~~

Success criterion:

> the same frozen core semantics survive architecture and platform changes without being redefined by host width or byte order.

## R10 — Evidence-gated CPU optimization

Only after reference semantics and measurements are stable:

- early working-set reduction;
- fused transforms/reductions;
- bounded SoA tiles;
- reusable scratch;
- exact lookup compression;
- invariant-bound reuse;
- optional compiler autovectorization/SIMD;
- optional bounded threading;
- optional host-aware path calibration.

Promotion requires:

1. exact reference parity where the contract requires exactness;
2. environment-scoped measurement;
3. lifecycle and memory costs included;
4. a material benefit under the declared workload;
5. rollback to a correct simpler path when the optimized path is unavailable or loses.

No optimization becomes architecture merely because one donor or one host benefited.

## R11 — Long-lived compatibility policy

Define:

- stable ABI/version policy;
- machine-contract versioning;
- serialized-format compatibility;
- target retirement rules;
- archived conformance vectors;
- release evidence bundles;
- constitutional migration rules.

## Roadmap rule

A later phase must not distort an earlier contract.

If a simpler implementation exposes a flaw in the architecture, fix the architecture before layering on more machinery.
