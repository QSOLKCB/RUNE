# RUNE Donors v1

**Status: R0 provenance map.**

RUNE is informed by mechanisms observed in other QSOLKCB repositories. Donors are precedent and evidence sources, not automatic implementation authority.

General rule:

~~~text
DONOR MECHANISM != RUNE IMPLEMENTATION
DONOR CONSTANT != RUNE CONSTANT
DONOR BENCHMARK != RUNE BENCHMARK
DONOR SOURCE != AUTOMATICALLY REUSABLE SOURCE
~~~

Every promoted mechanism must be independently justified under RUNE's C99, CPU-only, integer-only, bounded-memory, licensing, and evidence contracts.

## OPT

Source: https://github.com/QSOLKCB/OPT

Useful mechanisms:

- early working-set reduction;
- reference-first optimization;
- bounded hot-field SoA tiling;
- evidence-gated SIMD/autovectorization;
- invariant-driven reuse;
- complete effective-input identity for reuse;
- calibrated host-aware path promotion;
- performance regression budgets.

RUNE adoption:

- use OPT as the optimization decision catalogue;
- preserve scalar/reference parity before promotion;
- measure complete applicable lifecycle and memory cost rather than kernel-only wins;
- keep reuse bound to complete effective identity.

Not inherited:

- target-specific benchmark values;
- tuning constants;
- worker caps;
- cache keys;
- thresholds.

## QSOL-MESH

Source: https://github.com/QSOLKCB/QSOL-MESH

Useful mechanisms:

- explicit allocation ownership and lifetime;
- transfer identity as source + destination + byte extent;
- peak-live memory distinct from cumulative allocation traffic;
- stream/reduce/discard;
- constant-size reusable execution template over very large logical domains;
- planning evidence separated from physical execution evidence;
- deterministic regeneration preferred over bulk transfer when equivalent.

RUNE adoption:

- explicit ownership/lifetime/accounting vocabulary;
- bounded reusable region templates;
- plan/request != execution evidence;
- logical-domain metadata should remain constant-size where semantics permit.

Not inherited:

- GPU/VRAM domains;
- CUDA machinery;
- device-transfer constants;
- MESH scheduler/planner architecture;
- calibration results.

## GALAXY

Source: https://github.com/QSOLKCB/GALAXY

Useful mechanisms:

- memory-bounded logical addressing;
- stream -> reduce -> discard CPU experiments;
- fused reduction removing an intermediate materialized array;
- cache-scale microtile sweeps;
- exact integer LUT compression validated against canonical output;
- deterministic worker-index reduction;
- measured NUMA/locality sensitivity;
- separation of algorithmic working-set bytes from process overhead.

RUNE adoption:

- corpus tests for materialized versus fused reduction;
- microtile sweeps;
- exact representation-level parity for integer lookup/reconstruction;
- memory wins may be HOLD when wall time regresses materially;
- locality hypotheses require evidence and must not be overstated.

Not inherited:

- particle/galaxy semantics;
- BAM constants;
- LUT sizes;
- worker counts;
- tile sizes;
- benchmark timings;
- GPU paths.

## GLUBALL

Source: https://github.com/QSOLKCB/GLUBALL

Useful mechanisms:

- exact integer logical-to-rendered mapping;
- wider integer intermediates;
- fixed-point canonical parameters;
- deterministic contiguous half-open partitions;
- no allocation proportional to the logical domain where sampling suffices;
- stream/reduce/discard discipline;
- complete-output authority separated from compact performance diagnostics.

RUNE adoption:

- explicit-width integer arithmetic with wider intermediates where required;
- deterministic range geometry;
- logical scale != resident scale;
- compact diagnostics do not become correctness authority unless the contract says so.

Not inherited:

- GLUBALL geometry;
- floating-point boundary;
- GPU runtime architecture;
- runtime constants;
- benchmark observations.

## COSMO

Source: https://github.com/QSOLKCB/COSMO

Useful mechanisms:

- scalar/reference authority before bounded parallel execution;
- parallel reads from immutable prior state;
- deterministic contiguous partitions;
- restoration of canonical output order;
- explicit claim/evidence classes.

RUNE adoption:

- if bounded concurrency is ever introduced, completion order must not silently redefine semantic result order;
- immutable/snapshot input state is preferred for deterministic parallel work.

Not inherited:

- COSMO scientific/symbolic semantics;
- Python threading implementation;
- Lean infrastructure.

## PSYCLE-LINUX

Source: https://github.com/QSOLKCB/PSYCLE-LINUX

Useful mechanism:

- historical ring-buffer design separates cursor state from actual backing storage;
- explicit memory ordering variants demonstrate that storage ownership and synchronization policy need not be one object.

RUNE adoption:

- caller-owned ring storage with separate read/write cursor state is a design precedent for R5.

Licensing boundary:

- imported historical Psycle material includes GPL-family source;
- RUNE/MPL-2.0 treats that source as mechanism/provenance evidence only unless an explicit compatible licensing analysis permits a specific use;
- do not copy source expressions into RUNE merely because the mechanism is useful.

Not inherited:

- audio engine semantics;
- plugin architecture;
- thread primitives;
- historical source code.

## UFT-ID-3.0

Source: https://github.com/QSOLKCB/UFT-ID-3.0

Useful mechanisms:

- typed transformation pipelines;
- deterministic replay contract;
- canonical content identity;
- explicit distinction between representation and referent;
- deterministic lexicographic recovery/tie-breaking vocabulary.

RUNE adoption:

- intermediate execution stages remain typed/identified rather than being silently collapsed into "buffer";
- proof receipts should identify canonical input, implementation/version, parameters, randomness status, output canonicalization, and result identity where applicable.

Not inherited:

- physical or informational theory claims;
- mathematical model semantics;
- scientific interpretations.

## C64

Source: https://github.com/QSOLKCB/C64

Useful architectural precedent:

- experience/front-end layer remains separate from the underlying execution engine;
- narrow adapter boundary avoids absorbing mature engine semantics into the UI layer.

RUNE adoption:

- reinforces the RIVET -> RUNE separation;
- a consumer may depend heavily on an engine without owning or redefining the engine.

Not inherited:

- VICE integration;
- Python front-end architecture;
- emulator semantics.

## Promotion rule

A donor mechanism may enter RUNE only when all of the following are explicit:

1. the RUNE problem it solves;
2. the correctness contract it preserves;
3. the memory/resource contract it changes;
4. the licensing boundary;
5. the reference path;
6. the RUNE-specific validation;
7. the environment-scoped evidence required for any performance claim, including complete applicable lifecycle and memory costs rather than kernel-only timing.

If those are not known, the donor remains research material.
