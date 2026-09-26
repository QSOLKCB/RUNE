# R2 — Arena and Scratch Lifetime

R2 adds controlled transient storage on top of the R1 bounded region model.

The arena is deliberately not a general heap. It borrows one caller-owned read/write span and advances a monotonic cursor through that bounded storage.

## Core model

~~~text
caller-owned region
      |
      v
bounded read/write storage span
      |
      v
rune_arena
  cursor
  high_water
  cumulative_payload_bytes
  cumulative_consumed_bytes
  allocation_count
  generation
      |
      v
bounded scratch spans
~~~

No allocation escapes the declared storage span.

## Alignment

rune_arena_alloc() accepts a non-zero power-of-two alignment.

Alignment is **relative to the beginning of the arena storage span**.

This is intentionally not a universal native-object-alignment claim. A caller that requires native type alignment must provide backing storage whose base satisfies the required native alignment. RUNE does not infer such a guarantee from an arbitrary byte pointer.

## Allocation

A non-zero successful allocation:

- aligns the current arena-relative cursor;
- verifies the aligned extent fits the arena storage;
- returns a read/write R1 span;
- advances the cursor;
- updates lifetime high-water;
- adds payload and consumed bytes to cumulative accounting;
- increments allocation count.

Exhaustion returns RUNE_ERR_EXHAUSTED and leaves arena state unchanged.

Arithmetic/accounting overflow returns RUNE_ERR_OVERFLOW and leaves arena state unchanged.

A zero-length allocation returns an aligned empty span but consumes no arena state and contributes no allocation traffic.

## Peak live versus cumulative traffic

The counters intentionally distinguish two quantities:

~~~text
current live consumed bytes = cursor
peak live consumed bytes    = high_water

cumulative payload bytes
cumulative consumed bytes   = payload + alignment padding over successful
                              non-zero allocations, including reused space
~~~

Checkpoint restore and reset may reduce current live bytes, but they do not reduce high-water or cumulative counters.

~~~text
PEAK LIVE BYTES != CUMULATIVE ALLOCATION TRAFFIC
~~~

## Checkpoints

rune_arena_checkpoint_save() records the arena object identity, arena incarnation, current cursor, and generation.

The arena pointer and incarnation token in a checkpoint are transient process-local guards. They are not portable serialized RUNE identity. Each successful rune_arena_init() receives a new incarnation token, so reinitializing the same arena object invalidates every checkpoint from its prior incarnation. The arena object's address must remain stable while checkpoints derived from the current incarnation are in use.

rune_arena_checkpoint_restore() may only rewind to a checkpoint from the same arena object and current generation whose cursor is not ahead of the current cursor.

A rejected checkpoint returns RUNE_ERR_STALE_CHECKPOINT.

rune_arena_alloc() also rejects an output pointer that aliases arena.storage. Allocation output is allowed to describe bytes within the backing storage, but it may not overwrite the arena's own storage descriptor.

Restoring a checkpoint does not clear bytes and does not reduce historical accounting.

All scratch spans allocated after the restored checkpoint are logically expired. R1 spans do not carry lifetime generations, so consumers must not use expired scratch spans after rewind.

## Reset

rune_arena_reset() rewinds the cursor to zero and increments the generation so prior checkpoints become stale.

Reset preserves the underlying bytes, high-water mark, and cumulative counters. It is lifetime control, not memory clearing. All arena-returned scratch spans from the prior generation are logically expired after reset.

If the generation counter cannot advance without overflow, reset fails explicitly.

## Out of scope

R2 does not add individual free, coalescing, a general-purpose heap, destructors, automatic clearing, threads, synchronization, SIMD, fixed-point arithmetic, or performance benchmarks.

## Build and test

~~~sh
make test
make clean test CC=clang
~~~

R1 and R2 deterministic correctness suites both run.

R2 makes no runtime performance claim.
