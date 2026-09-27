# R6 — Deterministic Operation Descriptors and Receipts

R6 defines the smallest reusable execution description demonstrated by real RUNE operations.

It is **not** a task graph, scheduler, command bus, or generic bytecode VM.

The abstraction is earned by two existing R1 operations:

~~~text
BYTES_MOVE
BYTES_FILL
~~~

## Public surface

~~~text
include/rune/operation.h
src/operation.c
~~~

The public model contains:

- a fixed-width operation identity;
- an explicit semantic version;
- bounded input/output span references;
- one versioned integer parameter lane;
- an explicit scratch requirement;
- caller-supplied region-slot bindings;
- an execution receipt.

## Descriptor identity

A span reference is:

~~~text
region_slot : uint32
offset      : uint64
length      : uint64
~~~

The descriptor contains no native pointer.

~~~text
portable descriptor
        |
        +-- operation_id
        +-- semantic_version
        +-- input span ref
        +-- output span ref
        +-- parameter_u64
        +-- scratch_required
~~~

Descriptor identity is computed by hashing those fields individually in a fixed byte order.

RUNE never hashes the raw C structure bytes, so compiler padding is not semantic identity.

~~~text
NATIVE ADDRESS != OPERATION IDENTITY
~~~

## Region bindings

At replay time, the caller supplies:

~~~text
region slot 0 -> rune_region object
region slot 1 -> rune_region object
...
~~~

Those native bindings are **execution context**, not descriptor identity.

The same descriptor replayed against equivalent bytes at different native addresses must produce the same descriptor identity and execution receipt.

## Version 1 operations

### BYTES_MOVE

~~~text
operation_id      = 1
semantic_version  = 1
input             = required
output            = required
input.length      = output.length
parameter_u64     = 0
scratch_required  = 0
~~~

Execution resolves a readable input span and writable output span, then delegates to the existing R1 overlap-safe move semantics.

Receipt accounting:

~~~text
bytes_read    = input.length
bytes_written = output.length
scratch_used  = 0
~~~

### BYTES_FILL

~~~text
operation_id      = 2
semantic_version  = 1
input             = canonical NONE
output            = required
parameter_u64     = fill byte 0..255
scratch_required  = 0
~~~

Execution resolves a writable output span and delegates to the existing R1 fill semantics.

Receipt accounting:

~~~text
bytes_read    = 0
bytes_written = output.length
scratch_used  = 0
~~~

The input NONE identity is exactly:

~~~text
region_slot = UINT32_MAX
offset      = 0
length      = 0
~~~

## Scratch

R6 freezes an explicit scratch requirement field even though both v1 operations require zero bytes.

That demonstrates the field without inventing a scratch protocol before an operation actually needs one.

Any non-zero scratch requirement on these v1 operations is rejected.

## Execution receipt

A receipt records:

~~~text
operation_id
semantic_version
status
execution_class
descriptor_identity
result_identity
bytes_read
bytes_written
scratch_required
scratch_used
~~~

Execution classes are:

~~~text
0 = rejected
1 = executed
~~~

A rejected descriptor or binding produces:

~~~text
execution_class = rejected
result_identity = 0
bytes_read      = 0
bytes_written   = 0
scratch_used    = 0
~~~

A successful execution hashes the exact output span bytes into result_identity.

Timing, native addresses, process IDs, thread IDs, and host-specific pointer values are excluded.

## Status identity

R6 adds:

~~~text
RUNE_ERR_UNSUPPORTED_OPERATION
~~~

for an unknown operation identity or unsupported semantic version.

Bounds/access failures retain the existing R1 status vocabulary.

A receipt records what happened; a requested or planned operation is not execution evidence.

## Replay proof

The checked-in proof executable is:

~~~text
proof/r6_proof.c
~~~

It freezes three deterministic replay vectors:

1. successful four-byte move across two region slots;
2. successful three-byte fill;
3. rejected out-of-bounds fill.

The proof vectors include explicit initial byte state and expected descriptor/result identities.

The canonical proof receipt fixture is:

~~~text
proof/r6-proof-receipts.v1.tsv
~~~

Its fixed columns are:

~~~text
vector_id
operation_id
semantic_version
status
execution_class
descriptor_identity
result_identity
bytes_read
bytes_written
scratch_required
scratch_used
~~~

All values are unsigned decimal ASCII separated by byte 0x09 and terminated by byte 0x0A.

The proof executable emits the same canonical format without relying on the host execution character set.

CI executes all replay vectors and byte-compares the generated receipt file against the checked-in fixture.

## Address-independent replay

R6 deterministic tests execute equivalent descriptors against separate region objects backed by different local arrays.

The test requires equal:

- descriptor identity;
- status;
- execution class;
- result identity;
- byte accounting;
- scratch accounting;
- output bytes.

This establishes that native addresses are not hidden operation identity.

## Existing local execution evidence

[rune_run.md](rune_run.md), committed after R5 merged, records a local run of the merged R5 tree.

It records:

- the default cc path compiling successfully;
- R1, R2, R3, and R5 deterministic tests passing;
- the frozen R4 smoke fingerprint gate passing;
- local R4 corpus receipts across the working-set ladder.

It also records that the requested local CC=clang build could not start because clang was not installed on that machine.

That is an **environment/tool availability limitation**, not a RUNE correctness failure. GitHub CI remains separate execution evidence for GCC and Clang.

The local R5 log is not retroactively treated as R6 execution evidence.

## Deliberate exclusions

R6 does not add:

- a task graph;
- dependency graph;
- scheduler;
- worker pool;
- asynchronous execution;
- dynamic operation registration;
- plugin operations;
- pointer-based portable identity;
- arbitrary serialized C-struct images;
- performance timing;
- performance claims.

A new operation must be earned by a real RUNE workload and explicit result contract.

## Build and test

~~~sh
make test
make r6-proof
make corpus-smoke
~~~

R1 through R6 deterministic tests run together, the R6 replay receipt is frozen, and the R4 corpus fingerprint remains unchanged.

R6 makes no runtime performance claim.
