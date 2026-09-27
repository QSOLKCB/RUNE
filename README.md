# RUNE

## Runtime for Unified Numeric Execution

**Memory-first, integer-only, CPU-only execution for bounded state and explicit data movement.**

> **Bound memory. Move data explicitly. Execute deterministically.**

RUNE is a small portable execution substrate for software that should do the least necessary work.

Its starting point is not "how much compute is available?" but:

1. where is the information;
2. how much state must be resident;
3. what must move;
4. what must actually be computed;
5. what can be reduced, regenerated, reused, or discarded.

RUNE is intended to sit below higher-level frameworks such as [RIVET](https://github.com/QSOLKCB/RIVET), while remaining independently useful.

## Core invariant

> **RUNE-INV-001 — Minimal Sufficient Execution**
>
> For a fixed correctness contract, prefer the representation, algorithm, and bounded state that require the least necessary computation, memory movement, residency, and abstraction. Additional compute resources do not justify unnecessary work.

In plain language:

~~~text
right representation
+ right algorithm
+ tiny bounded state
+ no unnecessary abstraction

can beat

enormous general compute
+ wrong execution model
~~~

That is not a universal performance claim. It is the design rule RUNE is built to test.

~~~text
MORE COMPUTE != BETTER EXECUTION
~~~

## Status

**R5 — Queues, rings and bounded streaming.**

R0 through R4 are merged. RUNE now includes a byte-oriented fixed-capacity FIFO over caller-owned storage, with cursor state separated from ownership, explicit wrapped two-span read/write views, and exact produce/consume commits.

R5 remains **single-threaded and correctness-only**. It adds no atomics, blocking semantics, scheduler, typed queue hierarchy, or runtime performance claim.

See [R5-RINGS-STREAMING.md](R5-RINGS-STREAMING.md) and [ROADMAP.md](ROADMAP.md).

## Build and test

~~~sh
make test
make clean test CC=clang
make corpus-smoke
./build/rune_corpus --profile local
~~~

The tests exercise deterministic correctness only. They are not performance benchmarks.

## Project identity

~~~text
Project: RUNE
Name: Runtime for Unified Numeric Execution
Reference language: ISO C99
Current phase: R5 — Queues, rings and bounded streaming
Architecture: CPU-only
Numeric boundary: integer-only
Memory model: explicit and bounded
Execution model: deterministic, reference-first
Primary integration target: RIVET
GPU runtime: none
Licence: MPL-2.0
~~~

## Architectural direction

~~~text
+--------------------------------------------------+
| APPLICATION / HIGHER-LEVEL FRAMEWORK             |
| RIVET / parsers / tools / native applications    |
+--------------------------+-----------------------+
                           |
                     narrow RUNE ABI
                           |
                           v
+--------------------------------------------------+
| RUNE                                             |
|--------------------------------------------------|
| bounded regions and spans                        |
| arenas and scratch lifetime                      |
| explicit offsets and movement                    |
| integer / fixed-point primitives                 |
| queues, rings and streaming                      |
| deterministic execution descriptions             |
| resource accounting                              |
+--------------------------+-----------------------+
                           |
                           v
+--------------------------------------------------+
| PLATFORM / CPU / MEMORY                          |
+--------------------------------------------------+
~~~

RUNE must not acquire application meaning merely because an application uses it. Browser, HTML, CSS, document, UI, window, image-format, and application semantics remain above the RUNE boundary.

## Why C99

C99 is the portability baseline, not an incidental implementation choice.

The core must not require:

- C++;
- compiler extensions for correctness;
- exceptions;
- garbage collection;
- a managed runtime;
- JIT compilation;
- mandatory threads;
- mandatory SIMD;
- floating-point arithmetic;
- OS-specific APIs.

Platform adapters may use target facilities behind explicit boundaries.

Portable semantic identities should use explicit-width integer contracts. Host mechanics such as pointer arithmetic may use host types where necessary, but host width must not silently redefine portable RUNE state.

## Runtime doctrine

RUNE starts from a few hard rules:

- externally driven resident memory growth must be bounded;
- ownership and lifetime must be explicit;
- movement of meaningful byte extents must be visible;
- logical scale must not imply proportional resident scale;
- transient representations should prefer stream -> consume/reduce -> discard where semantics permit;
- cheap deterministic state may be regenerated instead of retained when evidence supports it;
- resource exhaustion is an explicit result, not permission to exceed a budget;
- successful result identity is separate from execution-plan identity;
- benchmark observations are separate from correctness identity;
- the scalar/single-threaded reference path comes before optimized paths;
- work elimination comes before acceleration;
- the second real implementation earns an abstraction.

The complete frozen R0 invariant set is in [CONSTITUTION.md](CONSTITUTION.md).

## Integer-only boundary

RUNE's public execution semantics do not require floating point.

Fractional quantities must use an explicit representation such as:

- fixed-point;
- scaled integers;
- integer numerator/denominator pairs;
- another versioned integer representation with declared rounding.

No one fixed-point format is universal.

Mathematical equivalence is not assumed to imply representation-level equivalence. Integer transforms, lookup compression, interpolation, and reconstruction must be checked against their declared reference contract.

## Memory-first model

The fundamental shape is a bounded region and validated view, not an arbitrarily connected heap graph.

Conceptually:

~~~text
REGION
  +-- capacity
  +-- ownership
  +-- lifetime
  +-- alignment
  +-- flags

SPAN
  +-- region identity
  +-- offset
  +-- length
  +-- access mode
~~~

Exact ABI structures are intentionally not frozen by this README. R1 must earn them.

Persistent relationships should prefer explicit region-relative identities or offsets where that materially improves validation, relocatability, compactness, or portability. Native pointers are not forbidden; they simply do not automatically become portable identity.

## Local memory corpus

RUNE will include a deterministic, procedurally generated **RUNE-CORPUS-v1** for ordinary local CPU testing.

The corpus is intentionally designed to expose memory behaviour rather than reward the largest machine. Planned workload families include:

- sequential and strided scans;
- deterministic permutation access;
- offset chasing;
- materialize-then-reduce versus fused reduction;
- AoS versus bounded hot-field SoA;
- cache-scale microtiles;
- caller-owned ring storage;
- bounded arena/reset patterns;
- fixed-point lookup versus recomputation;
- retain-versus-regenerate;
- RIVET-like byte streams;
- huge logical domains with constant-size resident metadata.

The corpus will use explicit working-set sizes rather than pretending a fixed byte size always corresponds to L1, L2, or L3 on every machine.

The point is to discover crossovers such as:

~~~text
RECOMPUTE MAY BE CHEAPER THAN FETCH
LESS RESIDENT STATE MAY BE FASTER
MORE WORKERS MAY STOP HELPING
~~~

Those are hypotheses to measure, not conclusions to assume.

See [ROADMAP.md](ROADMAP.md).

## Relationship to RIVET

RIVET remains the higher-level framework.

~~~text
RIVET
  |
  | bounded portable execution boundary
  v
RUNE
  |
  v
CPU + MEMORY + OS
~~~

RIVET owns application-facing semantics such as commands, UI, documents, HTML/CSS, browser behaviour, history, bookmarks, downloads, and software-raster meaning.

RUNE may own lower-level machinery such as bounded storage, regions, arenas, queues, rings, fixed-point primitives, transforms, scratch lifetime, accounting, and deterministic CPU execution.

RUNE must remain useful without RIVET, and RIVET integration is deliberately deferred until the substrate proves itself independently.

## Donor research

RUNE is informed by mechanisms observed in:

- [OPT](https://github.com/QSOLKCB/OPT)
- [QSOL-MESH](https://github.com/QSOLKCB/QSOL-MESH)
- [COSMO](https://github.com/QSOLKCB/COSMO)
- [GALAXY](https://github.com/QSOLKCB/GALAXY)
- [GLUBALL](https://github.com/QSOLKCB/GLUBALL)
- [PSYCLE-LINUX](https://github.com/QSOLKCB/PSYCLE-LINUX)
- [UFT-ID-3.0](https://github.com/QSOLKCB/UFT-ID-3.0)
- [C64](https://github.com/QSOLKCB/C64)

Donors provide precedent, not automatic architecture.

RUNE does not inherit donor constants, worker counts, cache sizes, tile sizes, benchmark results, source code, or performance claims merely because a mechanism was useful elsewhere.

See [DONORS-v1.md](DONORS-v1.md).

## Human and machine authority

Human-facing project prose is kept separate from machine-readable contracts.

Human-facing files:

- [README.md](README.md)
- [CONSTITUTION.md](CONSTITUTION.md)
- [ROADMAP.md](ROADMAP.md)
- [DONORS-v1.md](DONORS-v1.md)
- [R1-REGION-CORE.md](R1-REGION-CORE.md)
- [R2-ARENA-SCRATCH.md](R2-ARENA-SCRATCH.md)
- [R3-INTEGER-NUMERIC.md](R3-INTEGER-NUMERIC.md)
- [R4-CORPUS-v1.md](R4-CORPUS-v1.md)
- [R5-RINGS-STREAMING.md](R5-RINGS-STREAMING.md)

Machine-facing files:

- [AGENTS.md](AGENTS.md)
- [machine/project-v1.json](machine/project-v1.json)
- [machine/invariants-v1.json](machine/invariants-v1.json)
- [machine/donors-v1.json](machine/donors-v1.json)
- [machine/r1-region-core.v1.json](machine/r1-region-core.v1.json)
- [machine/r2-arena-scratch.v1.json](machine/r2-arena-scratch.v1.json)
- [machine/r3-integer-numeric.v1.json](machine/r3-integer-numeric.v1.json)
- [machine/r4-corpus-v1.json](machine/r4-corpus-v1.json)
- [machine/r5-rings-streaming.v1.json](machine/r5-rings-streaming.v1.json)

A contradiction between the two surfaces is a defect. Automated agents must fail closed rather than invent a reconciliation.

## Non-goals

RUNE is not intended to become a:

- web browser;
- GUI framework;
- graphics API;
- operating system or kernel;
- garbage-collected runtime;
- JavaScript runtime;
- JVM/CLR replacement;
- general-purpose VM;
- JIT compiler;
- database;
- actor or async framework;
- distributed scheduler;
- GPU compute runtime;
- plugin ecosystem.

If another layer can own a concern cleanly, RUNE should not absorb it.

## Licence

RUNE is licensed under the **Mozilla Public License 2.0 (MPL-2.0)**.

See [LICENSE](LICENSE).

## Closing principle

Most software asks:

> **What computation should run?**

RUNE asks first:

> **Where is the information now?**

> **What must move?**

> **What can disappear?**

Then it computes what remains necessary.
