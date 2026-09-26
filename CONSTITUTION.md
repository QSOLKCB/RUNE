# RUNE Constitution v1

**Status: R0 authority.**

This document defines the non-negotiable architectural invariants for the first RUNE implementation line. Later work may refine implementation details, but changing an invariant requires an explicit versioned constitutional change.

## RUNE-INV-001 — Minimal Sufficient Execution

For a fixed correctness contract, prefer the representation, algorithm, and bounded state that require the least necessary computation, memory movement, residency, and abstraction.

Additional compute resources do not justify unnecessary work.

~~~text
right representation
+ right algorithm
+ tiny bounded state
+ no unnecessary abstraction

can beat

enormous general compute
+ wrong execution model
~~~

This is a design invariant, not a universal benchmark claim.

## RUNE-INV-002 — Bounded External Growth

Externally supplied input may not silently cause unbounded resident memory growth.

Operations whose resident state can grow with external input must have an explicit capacity, budget, bounded window, or explicit resource-exhaustion result.

## RUNE-INV-003 — Integer-Only Public Execution Semantics

The portable RUNE execution contract does not require floating-point arithmetic or floating-point environment state.

Fractional semantics must use an explicit versioned integer representation and declared rounding behaviour.

## RUNE-INV-004 — Explicit Ownership, Lifetime and Movement

Managed storage must have explicit ownership and lifetime.

Meaningful data movement must identify its source, destination, and extent where the abstraction can represent them.

Hidden copying is not a portability feature.

## RUNE-INV-005 — Logical Scale Is Not Resident Scale

A large logical domain does not justify resident state proportional to that complete domain when bounded windows, procedural regeneration, compact indices, streaming, or reduction preserve the required semantics.

## RUNE-INV-006 — Result Identity Is Separate From Execution Plan

For semantics declared invariant, a successful result must not change merely because chunk size, scratch budget, worker count, internal scheduling, cache shape, CPU family, or an optional optimized path changes.

An execution may fail explicitly when the declared resource budget cannot satisfy the minimum valid representation.

~~~text
RESULT IDENTITY != EXECUTION PLAN IDENTITY
BENCHMARK OBSERVATION != CORRECTNESS IDENTITY
~~~

## RUNE-INV-007 — Reference Before Optimization

The smallest practical scalar, deterministic, single-threaded implementation is the semantic reference until evidence earns another path.

An optimized path must preserve the declared result contract or fail closed.

## RUNE-INV-008 — Eliminate Work Before Accelerating It

Optimization order is:

1. choose the representation;
2. choose the algorithm;
3. bound resident state;
4. remove unnecessary materialisation and movement;
5. remove duplicate work;
6. improve locality;
7. measure;
8. only then consider SIMD, threading, topology tuning, or specialized CPU paths.

A faster unnecessary operation is still unnecessary work.

## RUNE-INV-009 — ISO C99 Portability Baseline

The reference runtime targets ISO C99.

Core correctness may not depend on C++, managed runtimes, JIT compilation, garbage collection, exceptions, mandatory threads, mandatory SIMD, or compiler-specific language extensions.

Platform-specific facilities must remain behind explicit boundaries.

## RUNE-INV-010 — CPU-Only Runtime

RUNE targets CPUs and memory.

RUNE has no GPU execution contract and no planned CUDA, OpenCL, Vulkan Compute, Metal, DirectCompute, WebGPU, or shader-compute backend.

A consumer may use such systems independently; that does not make them RUNE.

## RUNE-INV-011 — Resource Failure Is Explicit

Bounds failure, overflow, impossible alignment, exhausted scratch, insufficient destination capacity, or unsupported capability must fail explicitly.

RUNE may not silently exceed a budget, wrap an invalid extent, or truncate unless truncation is the declared operation.

## RUNE-INV-012 — Higher-Level Semantics Stay Above RUNE

RUNE does not define browser, HTML, CSS, document, GUI, window, image-format, application, or business semantics.

A general primitive may support those systems without absorbing their meaning.

## RUNE-INV-013 — The Second Implementation Earns the Abstraction

Do not create generic scheduler, executor, allocator, backend, cache, plugin, dependency-injection, or task-graph hierarchies for hypothetical future use.

A second real implementation must demonstrate the common abstraction before it is promoted.

## RUNE-INV-014 — Performance Claims Are Environment-Scoped

No benchmark result is universal.

A performance observation must identify the relevant source revision, workload, compiler/toolchain, build configuration, CPU/platform, memory profile, and measurement method.

A donor benchmark is not a RUNE benchmark.

## RUNE-INV-015 — Donor Provenance Does Not Transfer Authority

Donor repositories may motivate mechanisms and tests.

RUNE does not inherit donor constants, worker counts, memory limits, cache sizes, tile sizes, source expressions, or performance claims unless they are independently justified for RUNE and licensing permits the use.

Source under incompatible licensing is mechanism/provenance evidence only unless an explicit compatible use is established.

## RUNE-INV-016 — Recompute Is A Valid Memory Strategy

Cheap deterministic state may be recomputed instead of retained or fetched when the correctness contract is preserved.

Recomputation is neither automatically better nor automatically worse. Promotion requires RUNE-specific measurement.

~~~text
RECOMPUTE MAY BE CHEAPER THAN FETCH
~~~

## RUNE-INV-017 — Portable Identity Uses Explicit Width

Host types such as size_t and native pointers may be used for local mechanics where required, but they do not automatically define portable RUNE identities.

Portable offsets, counts, serialized fields, and externally visible numeric identities must use explicit versioned widths or another explicit representation.

## RUNE-INV-018 — Requested Or Planned Work Is Not Execution Evidence

A plan, requested capability, advertised CPU feature, cross-compile, or generated receipt template does not prove that the corresponding execution occurred.

Evidence must state what actually executed and at what evidence class.

## RUNE-INV-019 — Human And Machine Authority Stay Separate

Human-facing prose and machine-readable contracts are maintained as separate repository surfaces.

A contradiction between them is a repository defect. Automated agents must fail closed and report the contradiction rather than silently choose or synthesize a third interpretation.

## RUNE-INV-020 — Smaller Wins Only When Correctness Is Equal

When two implementations satisfy the same contract with equivalent correctness, safety, portability, readability, and evidence, prefer the materially smaller and simpler implementation.

This is not code golf. Smaller code that weakens any of those properties does not win.

## Constitutional change rule

Changing an invariant requires:

- a new constitutional version or explicit amendment;
- corresponding machine-contract changes;
- a migration statement for affected runtime contracts;
- evidence that existing frozen semantics are either preserved or intentionally superseded.

Implementation convenience is not sufficient reason to mutate an invariant.
