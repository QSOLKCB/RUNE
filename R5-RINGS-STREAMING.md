# R5 — Queues, Rings and Bounded Streaming

R5 adds the first reusable dataflow primitive to the RUNE runtime: a byte-oriented fixed-capacity FIFO ring over caller-owned R1 storage.

It is intentionally smaller than a general queue framework.

## Public surface

~~~text
include/rune/ring.h
src/ring.c
~~~

The R5 reference ring provides:

- caller-owned borrowed storage;
- fixed byte capacity;
- explicit read/write cursor state;
- exact readable byte count;
- exact writable byte count;
- wrapped readable views as at most two R1 spans;
- wrapped writable views as at most two R1 spans;
- exact produce commit;
- exact consume commit.

## Ownership

The caller owns the bytes.

~~~text
caller-owned region/span
        |
        v
rune_ring.storage  (borrowed)
        |
        +-- read_cursor
        +-- write_cursor
        +-- size
~~~

RUNE never allocates, frees, resizes, or relocates the backing storage.

The ring state describes **where logical FIFO data begins, where new bytes may be committed, and how many bytes are live**.

~~~text
CURSOR STATE != STORAGE OWNERSHIP
~~~

## Capacity

Capacity is exactly:

~~~text
ring.storage.length
~~~

No slot is reserved to distinguish full from empty.

~~~text
empty: size == 0
full:  size == capacity
~~~

Therefore the full declared capacity is usable.

A zero-capacity ring is valid. It has zero readable and writable bytes; non-zero produce fails with RUNE_ERR_CAPACITY and non-zero consume fails with RUNE_ERR_OUT_OF_BOUNDS.

## FIFO state invariant

For non-zero capacity:

~~~text
0 <= read_cursor  < capacity
0 <= write_cursor < capacity
0 <= size         <= capacity

write_cursor == (read_cursor + size) mod capacity
~~~

R5 computes this relation without requiring an overflowing native addition.

Malformed state is rejected before a view or commit is returned.

## Wrapped views

A logical readable or writable interval can wrap at most once around fixed storage.

R5 therefore returns:

~~~text
first span
second span
total_length
~~~

The logical order is always:

~~~text
first -> second
~~~

For readable views:

- both spans are read-only;
- total_length equals ring.size.

For writable views:

- both spans are write-only;
- total_length equals capacity - ring.size.

A non-wrapped interval has a zero-length second span.

## Produce and consume

The zero-copy protocol is:

~~~text
write_views
    -> caller writes bytes
    -> rune_ring_produce(exact_length)

read_views
    -> caller consumes/reduces bytes
    -> rune_ring_consume(exact_length)
~~~

rune_ring_produce() commits bytes that the caller has already written into the current writable view.

rune_ring_consume() discards bytes that the caller has already consumed from the current readable view.

Neither operation copies data.

Over-produce returns:

~~~text
RUNE_ERR_CAPACITY
~~~

Over-consume returns:

~~~text
RUNE_ERR_OUT_OF_BOUNDS
~~~

Both failures leave ring state unchanged.

A zero-length commit is a successful no-op.

## View lifetime

R1 spans do not carry ring-state generations.

Therefore any previously derived ring views are **logically expired after a successful non-zero produce or consume**.

The caller must reacquire views after ring state changes.

This is a single-threaded reference contract; R5 does not attempt to make stale zero-copy views safe under concurrent mutation.

## Bounded chunk proof

The R5 deterministic test processes:

~~~text
logical stream: 1,048,699 bytes
resident ring:          257 bytes
~~~

The producer procedurally generates bytes into writable spans.

The consumer reduces bytes from readable spans and discards them immediately.

The proof checks:

- every logical byte is produced;
- every logical byte is consumed;
- FIFO order is preserved across wraps;
- the producer and consumer reductions match exactly;
- ring occupancy never exceeds 257 bytes;
- the logical stream is much larger than resident ring state.

This demonstrates:

~~~text
stream -> consume/reduce -> discard
LOGICAL SCALE != RESIDENT SCALE
~~~

without allocating storage proportional to the logical stream.

## Single-thread baseline

R5 requires no:

- atomics;
- locks;
- memory-order primitives;
- worker threads;
- blocking waits;
- multi-producer or multi-consumer coordination.

Concurrent mutation is outside the R5 contract.

If a later real implementation requires concurrency, it must earn that abstraction separately.

## Deliberate exclusions

R5 does not add:

- typed item queues;
- variable-sized message framing;
- blocking queue semantics;
- async runtime integration;
- thread-safe ring semantics;
- lock-free algorithms;
- a scheduler;
- generic stream graphs;
- runtime performance claims.

The frozen R4 C10 corpus-local ring remains a workload witness; it is not silently redefined as the R5 runtime API.

## Build and test

~~~sh
make test
make clean test CC=clang
make corpus-smoke
~~~

R1 through R5 deterministic correctness tests run together, while the frozen R4 corpus identity remains unchanged.

R5 makes no runtime performance claim.
