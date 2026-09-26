# RUNE

## Runtime for Unified Numeric Execution

**Memory-first, integer-only CPU runtime for bounded, deterministic execution and explicit data movement.**

> **Bound memory. Move data explicitly. Execute deterministically.**

RUNE is a small portable execution substrate designed around the way software actually interacts with memory.

Rather than treating memory allocation, data movement, object lifetime, and working-set growth as incidental implementation details, RUNE makes them explicit parts of the execution model.

RUNE is:

- CPU-only;
- integer-only at its public execution boundary;
- deterministic by default;
- designed around bounded memory ownership;
- explicit about data movement;
- suitable for constrained and historical systems;
- independent of GUI, browser, document, and application semantics;
- intended to support higher-level frameworks such as [RIVET](https://github.com/QSOLKCB/RIVET).

RUNE is not a GUI toolkit, browser engine, operating system, virtual machine, managed runtime, or general-purpose application framework.

It is the layer underneath those things.

---

# Status

**Early architectural phase.**

The initial project goal is to establish and prove the execution model before adding performance-oriented machinery.

The first implementation should remain deliberately small.

No scheduler hierarchy, worker-pool framework, generic cache subsystem, plugin system, JIT compiler, garbage collector, GPU backend, or distributed runtime belongs in the initial architecture.

The first requirement is simpler:

> prove that useful computation can be expressed through bounded memory regions, explicit integer operations, deterministic ownership, and controlled data movement.

---

# Core thesis

Conventional software frequently treats memory as an invisible service behind computation:

```text
allocate object
    ↓
store pointers
    ↓
call functions
    ↓
allocate more objects
    ↓
follow pointers
    ↓
discard later
```

RUNE begins from a different assumption:

```text
memory region
    ↓
bounded view
    ↓
explicit operation
    ↓
bounded result
    ↓
retain / reduce / discard
```

The architecture is designed around four questions:

1. **Where does the data live?**
2. **How much memory may it occupy?**
3. **What operation may act on it?**
4. **What state must remain when that operation finishes?**

Memory residency is part of the execution contract.

---

# Architectural direction

```text
+--------------------------------------------------+
| APPLICATION                                      |
| domain-specific semantics                        |
+--------------------------+-----------------------+
                           |
                           v
+--------------------------------------------------+
| HIGHER-LEVEL FRAMEWORK                           |
| RIVET / tools / parsers / applications           |
+--------------------------+-----------------------+
                           |
                    narrow RUNE ABI
                           |
                           v
+--------------------------------------------------+
| RUNE                                             |
|                                                  |
| memory regions                                   |
| offsets and bounded spans                        |
| arenas and scratch storage                       |
| integer / fixed-point operations                 |
| queues and rings                                 |
| work descriptors                                 |
| explicit data movement                           |
| deterministic execution                         |
| memory accounting                                |
+--------------------------+-----------------------+
                           |
                           v
+--------------------------------------------------+
| PLATFORM / CPU                                   |
| OS memory + files + timers + CPU execution       |
+--------------------------------------------------+
```

RUNE must not acquire application meaning merely because an application uses it.

RUNE should not know what a:

- browser;
- HTML element;
- CSS rule;
- menu;
- button;
- document;
- image format;
- network protocol;
- bookmark;
- window;
- pixel;

means unless a primitive is genuinely general enough to belong below those concepts.

---

# Design principles

## 1. Memory is part of the computation

Memory is not treated as an unlimited anonymous resource.

Operations should know, where practical:

- input region;
- output region;
- scratch requirement;
- maximum resident working set;
- ownership;
- lifetime;
- alignment;
- access intent.

Externally supplied input must not silently produce unbounded resident state.

---

## 2. Integer-only execution boundary

The core RUNE execution model uses integer representations.

The public runtime contract should not require:

```text
float
double
long double
floating-point environment state
floating-point ABI compatibility
GPU arithmetic
```

Where fractional quantities are required, they should be represented explicitly.

Possible representations include:

```text
Q16.16
Q24.8
Q32.32
integer numerator / denominator
scaled integer units
```

No particular fixed-point representation is universal.

The representation should be chosen by the operation that actually requires it.

Integer-only does not mean arithmetic must be simplistic.

It means representation and rounding are explicit rather than inherited accidentally from a platform floating-point environment.

---

# 3. Bounded memory ownership

Memory growth must be visible.

Candidate memory forms include:

```text
fixed caller-owned region
bounded arena
bounded scratch region
ring buffer
fixed-capacity queue
bounded table
bounded slab
```

Failure to fit within a declared memory budget is a legitimate execution result.

The runtime must not silently exceed the budget in order to preserve apparent success.

---

# 4. Explicit data movement

Moving data has a cost.

RUNE should make meaningful movement visible rather than hiding it behind layers of object copying.

The preferred shape is:

```text
source region
      |
      v
validated view
      |
      v
operation
      |
      +----> destination region
      |
      +----> compact execution receipt
```

Avoid copying when a bounded view is sufficient.

Avoid retaining transformed data when it can be regenerated cheaply.

Avoid materialising an entire logical domain when only a bounded portion must be resident.

---

# 5. Stream → consume/reduce → discard

Transient representations should not automatically earn permanent residence.

Typical shape:

```text
input bytes
    ↓
decode
    ↓
consume useful information
    ↓
commit required state
    ↓
discard scratch
```

This principle is especially important for:

- parsers;
- document engines;
- image decoding;
- networking;
- procedural generation;
- large logical datasets;
- browser-like applications.

Persistent state remains persistent when later semantics genuinely require it.

---

# 6. Logical scale != resident scale

A workload may describe more information than should exist in memory simultaneously.

RUNE should favour:

```text
bounded windows
bounded chunks
procedural regeneration
compact indices
streamed transforms
incremental reduction
```

over structures whose resident memory grows automatically with the total logical domain.

---

# 7. Result identity != execution-plan identity

Where semantics are declared invariant:

```text
RESULT IDENTITY != EXECUTION PLAN IDENTITY
```

A successful result should not change merely because execution used:

- a different chunk size;
- a different scratch budget;
- an optional optimized integer implementation;
- different internal scheduling;
- a different supported CPU family.

Execution may fail explicitly when the declared resources cannot satisfy the minimum valid representation.

Resource exhaustion is not a conflicting correctness result.

It is an explicit inability to execute under the requested constraints.

---

# 8. Reference implementation first

Every important operation should begin with the simplest correct implementation practical.

Optimization should be earned.

Possible later optimization mechanisms include:

- tighter region reuse;
- reduced copying;
- cache-aware traversal;
- specialized integer kernels;
- SIMD integer operations;
- bounded threading;
- work coalescing;
- platform-specific implementations.

None should be promoted merely because the host CPU supports them.

The baseline path remains the semantic reference.

---

# 9. Work elimination before acceleration

Before making an operation faster:

1. remove unnecessary work;
2. reduce data movement;
3. reduce resident state;
4. avoid duplicate transformations;
5. improve locality;
6. measure;
7. only then consider accelerated paths.

A faster unnecessary operation is still unnecessary work.

---

# 10. The second implementation earns the abstraction

RUNE does not pre-create architecture for hypothetical future implementations.

Do not add:

- executor hierarchies;
- backend factories;
- generic allocator interfaces;
- plugin systems;
- scheduler abstractions;
- generalized caching;
- polymorphic operation graphs;

until a second real implementation demonstrates that the common abstraction is necessary.

Small explicit code is preferred over speculative symmetry.

---

# Memory model

The fundamental RUNE object should be closer to a bounded region than an arbitrarily interconnected object graph.

Conceptually:

```text
REGION
  |
  +-- base
  +-- capacity
  +-- used
  +-- alignment
  +-- ownership
  +-- lifetime
  +-- flags
```

Operations act through validated bounded views.

Conceptually:

```text
SPAN
  |
  +-- region
  +-- offset
  +-- length
  +-- element width
  +-- access mode
```

Exact structures are intentionally not frozen yet.

The implementation must earn them.

---

# Offset-oriented references

RUNE should investigate using region-relative offsets wherever persistent references do not require native pointers.

Instead of making this the persistent representation:

```c
struct node *next;
```

a bounded region may contain something conceptually closer to:

```c
uint32_t next_offset;
```

resolved through:

```text
region base + validated offset
```

Potential advantages include:

- relocatable memory regions;
- easier bounds validation;
- compact persistent state;
- 32-bit / 64-bit independence;
- deterministic hashing;
- binary snapshots;
- shared-memory experiments;
- historical CPU compatibility;
- reduced pointer-heavy structures.

Native pointers are not forbidden.

They simply should not become persistent architectural identity without reason.

---

# Execution model

The baseline RUNE runtime is expected to be single-threaded and deterministic.

A simple operation might conceptually look like:

```text
input span
    ↓
operation descriptor
    ↓
scratch region
    ↓
destination span
    ↓
result / receipt
```

An execution receipt may eventually record bounded facts such as:

```text
operation ID
input identity
output length
scratch used
bytes read
bytes written
status
```

Receipts should remain compact.

They are not intended to become verbose tracing machinery by default.

---

# Determinism

RUNE should make deterministic behaviour easy to test.

For the same:

```text
operation
input bytes
declared profile
memory budget
semantic parameters
```

a successful reference execution should produce the same correctness result.

Correctness must not depend on:

- heap address;
- process address-space layout;
- host wall-clock time;
- thread timing;
- floating-point mode;
- random allocator behaviour;
- CPU vendor;
- benchmark state.

Where nondeterminism is genuinely required, it should enter explicitly as input.

---

# Platform boundary

RUNE is not an operating system abstraction layer.

The platform boundary should remain narrow.

A host may eventually provide facilities such as:

```text
memory acquisition
monotonic timing
file access
thread creation
CPU feature discovery
```

but RUNE must not silently inherit every host facility as part of its semantics.

Platform capability and execution semantics remain separate.

---

# CPU-only doctrine

> **RUNE targets CPUs and memory, not accelerators.**

RUNE has no planned GPU execution path.

It should not require:

- CUDA;
- OpenCL;
- Vulkan Compute;
- Metal;
- DirectCompute;
- WebGPU;
- GPU shader languages.

A higher layer may use other systems independently.

That does not make them part of RUNE.

---

# Relationship to RIVET

[RIVET](https://github.com/QSOLKCB/RIVET) is a capability-driven interface, document, and application framework with CPU/software raster rendering.

RUNE is intended to sit below that layer.

Conceptually:

```text
RIVET
  |
  | portable bounded execution interface
  v
RUNE
  |
  v
CPU + MEMORY + OS
```

RIVET retains ownership of:

```text
commands
capabilities
UI semantics
documents
HTML
CSS
layout semantics
browser behaviour
history
bookmarks
downloads
view source
software raster semantics
```

RUNE may provide lower-level machinery for:

```text
bounded memory
regions
arenas
queues
rings
fixed-point arithmetic
data transforms
scratch lifetime
execution descriptors
resource accounting
CPU execution
```

RUNE must remain useful without RIVET.

RIVET must also retain a clear enough abstraction boundary that RUNE can be evaluated independently before integration.

---

# Initial research question

The project is intended to investigate the following hypothesis:

> **Can a software execution substrate designed from the outset around bounded memory residency, explicit data movement, integer computation, and deterministic ownership materially simplify or improve constrained interactive software compared with conventional pointer- and allocator-centric runtime design?**

Performance alone is not the only measure.

Relevant outcomes include:

- smaller resident working sets;
- fewer allocations;
- fewer copies;
- improved locality;
- simpler lifetime reasoning;
- stronger deterministic behaviour;
- easier architecture portability;
- simpler binary state;
- reduced implementation complexity;
- measurable CPU efficiency.

Claims require evidence.

---

# Initial roadmap

## R0 — Constitutional foundation

Define the non-negotiable architecture before substantial runtime implementation.

Goals:

- define project mission;
- freeze CPU-only doctrine;
- freeze integer-only boundary;
- define bounded-memory rules;
- define ownership vocabulary;
- define result/error model;
- define portability and evidence classes;
- establish explicit non-goals;
- publish machine-readable project identity.

No performance claims belong in R0.

---

## R1 — Region core

Establish the smallest useful memory model.

Candidate scope:

- caller-owned memory region;
- checked offset arithmetic;
- bounded span/view;
- explicit read/write access;
- overlap-safe movement;
- fill/clear primitives;
- deterministic validation;
- fixed-width integer types;
- overflow-safe size calculations.

Success criterion:

> useful memory operations execute without dynamic allocation or application-specific semantics.

---

## R2 — Bounded arena + scratch lifetime

Introduce controlled transient storage.

Candidate scope:

- monotonic bounded arena;
- explicit reset point;
- scratch scopes;
- alignment handling;
- exhaustion result;
- high-water accounting.

No general-purpose heap is required.

Success criterion:

> temporary execution state has measurable maximum residency and deterministic lifetime.

---

## R3 — Integer numeric core

Establish portable arithmetic primitives required by real consumers.

Candidate scope:

- checked add/subtract/multiply;
- saturating variants where justified;
- shifts and scaling;
- explicit rounding;
- fixed-point proof operations;
- ratio conversion;
- integer interpolation where earned.

No universal numerical framework is planned.

---

## R4 — Queues and streaming

Add bounded dataflow primitives.

Candidate scope:

- fixed-capacity FIFO;
- ring buffer;
- producer/consumer cursor;
- bounded chunk processing;
- stream → reduce → discard proof.

Success criterion:

> logical input larger than the active working set can be processed under a fixed resident-memory ceiling.

---

## R5 — Deterministic execution descriptors

Define the minimum reusable execution description required by real operations.

Candidate scope:

- operation identity;
- bounded input spans;
- bounded output spans;
- scratch requirement;
- execution status;
- compact resource accounting;
- deterministic proof vectors.

Avoid building a general task-graph system.

---

## R6 — Memory movement study

Measure actual movement and residency.

Candidate evidence:

- bytes read;
- bytes written;
- copies avoided;
- scratch high-water mark;
- peak resident region size;
- logical-domain size versus resident size;
- operation counts.

The project should learn whether the architecture's central premise is actually producing useful results.

---

## R7 — RIVET integration adapter

Only after the standalone model is proven:

- expose a narrow adapter suitable for RIVET;
- identify operations RIVET genuinely benefits from;
- preserve RIVET semantics;
- keep browser/document concepts outside RUNE;
- compare the adapter against RIVET's existing reference behaviour.

RUNE must earn its place beneath RIVET through evidence.

---

## Later phases

Possible future work, only when justified:

```text
specialized integer kernels
cache-aware memory layouts
bounded concurrency
SIMD integer paths
CPU feature calibration
persistent compact regions
snapshot formats
shared-memory execution
historical architecture ports
```

None are commitments merely because they are technically possible.

---

# Non-goals

RUNE is not intended to become:

- a web browser;
- a GUI framework;
- an HTML or CSS engine;
- a graphics API;
- an operating system;
- a kernel;
- a hypervisor;
- a garbage-collected runtime;
- a JavaScript runtime;
- a JVM or CLR replacement;
- a general-purpose VM;
- a JIT compiler;
- a database;
- an actor framework;
- an async framework;
- a distributed scheduler;
- a GPU compute runtime;
- a dependency-injection framework;
- a plugin ecosystem.

If another layer can own a concern cleanly, RUNE should not absorb it.

---

# Implementation language

The initial reference implementation should favour a language and subset that can remain small, explicit, portable, and inspectable.

C is a natural candidate because it provides:

- predictable integer representations when fixed-width types are available;
- direct control over memory;
- broad compiler availability;
- compatibility with constrained systems;
- simple foreign-function boundaries;
- straightforward inspection of generated behaviour.

The exact baseline language/version should be frozen by the first implementation contract rather than assumed permanently by this README.

---

# Portability direction

RUNE should avoid unnecessary assumptions about:

```text
pointer width
endianness
floating-point support
SIMD availability
thread availability
virtual memory
large address spaces
modern operating systems
```

Initial modern hosts are development environments, not definitions of the architecture.

Potential future evidence targets may include:

- x86-64;
- x86 32-bit;
- ARM;
- PowerPC;
- Motorola 68k;
- emulated historical targets;
- constrained modern systems.

A cross-compile is not execution evidence.

An emulator is not physical-hardware evidence.

A CPU-family pass is not an operating-system-family claim.

Evidence must say what actually ran.

---

# Security direction

Explicit memory bounds are part of the security model.

RUNE operations should fail closed on:

- arithmetic overflow;
- invalid offsets;
- invalid spans;
- out-of-range access;
- insufficient destination capacity;
- malformed descriptors;
- impossible alignment;
- exhausted scratch space;
- contradictory region identity.

No operation should convert a failed bounds check into implicit truncation unless truncation is itself the declared operation.

---

# Testing doctrine

Correctness tests should favour deterministic vectors.

Important classes include:

```text
zero-length regions
maximum valid offsets
one-past-end rejection
integer overflow
overlapping movement
alignment edges
arena exhaustion
ring wrap-around
chunk-boundary equivalence
endianness-sensitive representations
32-bit width limits
fixed-point rounding edges
```

Optimized implementations must match the reference contract.

Performance evidence never replaces correctness evidence.

---

# Measurement doctrine

RUNE should not call itself:

```text
fast
lightweight
low-memory
cache-efficient
high-performance
```

without measurement.

Useful measurements may include:

- binary size;
- stack usage;
- arena high-water mark;
- total resident memory;
- bytes moved;
- number of copies;
- cycles per operation;
- logical bytes processed;
- resident bytes required;
- reference versus optimized execution.

Results should identify:

```text
CPU
OS
compiler
compiler flags
RUNE commit
test workload
memory profile
```

Benchmark observations are not correctness identity.

---

# Repository direction

Likely source shape:

```text
include/
src/
tests/
proof/
bench/
docs/
evidence/
machine/
```

Directories should be created when real code requires them.

The repository should not be inflated to resemble a mature framework before the implementation exists.

---

# Project rules

1. **Bound externally driven memory growth.**
2. **Prefer explicit ownership.**
3. **Use integers at the runtime boundary.**
4. **Move only what must move.**
5. **Do not retain reconstructible state without evidence.**
6. **Keep the reference path simple.**
7. **Measure before optimizing.**
8. **Do not hide resource failure.**
9. **Do not absorb higher-level semantics.**
10. **The second implementation earns the abstraction.**

---

# Name

**RUNE** stands for:

> **Runtime for Unified Numeric Execution**

The name reflects the project's focus on a small numeric execution substrate rather than application or interface semantics.

---

# Project identity

```text
Project: RUNE
Name: Runtime for Unified Numeric Execution
Architecture: CPU-only
Numeric boundary: integer-only
Memory model: explicit and bounded
Execution model: deterministic reference-first
Primary integration target: RIVET
GPU runtime: none
```

---

# Licence

RUNE is licensed under the **Mozilla Public License 2.0 (MPL-2.0)**.

This matches RIVET's licensing while allowing RUNE to remain an independently reusable execution substrate.

See [LICENSE](LICENSE) for the full licence text.

---

# Closing principle

Most software asks:

> **What computation should run?**

RUNE adds two questions before it:

> **Where is the information now?**

and:

> **What must move for the computation to happen?**

The project begins there.
