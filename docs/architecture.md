# Architecture

## Why price-time priority, and why integer ticks

Limit order books match on **price priority** first (better price wins),
then **time priority** at the same price (first in, first matched). This
project encodes that directly in the data structures rather than as a
comparison function applied after the fact:

- `bids_` is a `std::map<PriceTicks, std::list<Order>, std::greater<>>` --
  highest price first, so `begin()` is always the best bid.
- `asks_` is a `std::map<PriceTicks, std::list<Order>>` -- lowest price
  first (default `less<>`), so `begin()` is always the best ask.
- Each price level is a `std::list<Order>` in insertion order, giving FIFO
  (time priority) within a level for free, plus O(1) `push_back` and
  stable iterators (needed for O(1) cancel -- see below).

Prices are stored as `int64_t` ticks (e.g. price * 100 for 2 decimal
places), never `double`. Two floating-point prices that are
"conceptually equal" can compare unequal, and a matching engine that gets
a price comparison wrong can cross orders incorrectly or fail to cross
orders that should match. Integer ticks make every comparison exact.

## O(log P + 1) cancel, not O(N)

A naive cancel scans every order on the book. This implementation keeps a
second index: `unordered_map<OrderId, Location>`, where `Location` stores
the side, price level, and a `std::list<Order>::iterator` directly into
that price level's list. Cancelling is then: hash lookup (O(1)) to find
the price level, tree lookup (O(log P), P = number of distinct price
levels) to find that level's list, then O(1) list erase using the stored
iterator. `std::list` iterators stay valid across insertions/erasures of
*other* elements, which is exactly why `std::list` was chosen over
`std::vector` for price levels despite the extra pointer-chasing cost.

## Why the book itself is single-threaded

Real trading venues run their matching engine on a single core/thread.
The reasoning: any lock protecting shared matching state serializes
access anyway, so you gain nothing from "multi-threading" the matching
logic itself -- you only add lock contention and unpredictable latency
(a thread can be blocked waiting for a lock at exactly the wrong moment).

This project follows that model:

- `OrderBook` has **no internal synchronization** and is not meant to be
  called from multiple threads. It doesn't need to be, because only one
  thread ever touches it.
- `MatchingEngine` owns one `OrderBook` and runs it on one dedicated
  `std::thread`. Producer threads never call into the book directly --
  they push a request onto `SpscQueue<EngineRequest>` and return
  immediately.
- The queue is a **lock-free single-producer/single-consumer ring
  buffer** (`spsc_queue.hpp`), using only atomics with acquire/release
  ordering. This is the boundary where concurrency lives; the matching
  logic behind it stays simple, sequential, and fast.

This is a real trade-off, not a free lunch: with one producer and one
consumer this works well, but scaling to multiple symbols means running
multiple independent `MatchingEngine` instances (one book, one thread,
one queue each) rather than sharing a single book across threads.

## Where the latency actually goes

`benchmarks/bench_matching.cpp` measures wall-clock throughput and, more
importantly, per-order matching latency (the time inside `run()` from
popping a request to finishing the book mutation). On this machine, a mix
of resting and crossing orders sustains roughly 1.5M orders/sec with a
p50 matching latency around 150-200ns and p99 in the low microseconds.

One thing worth calling out honestly: the **max** latency sample is
consistently much higher (single-digit milliseconds) than everything
below p99. That is not the matching algorithm -- `add_limit_order` and
`cancel_order` are both bounded by O(log P) map operations on a book with
a few hundred price levels, which cannot itself take milliseconds. The
likely cause is `std::map`'s per-node heap allocation: with 500K orders
each triggering a `new`/`delete` through glibc's allocator, an occasional
allocation needs a syscall (`mmap`/`brk`) instead of reusing a free
block, and that syscall can occasionally cost microseconds-to-low-
milliseconds, especially under a shared/virtualized CPU. A production
engine would replace the default allocator with a pre-allocated
object pool or arena for `Order` nodes to remove this tail entirely --
noted here as a real limitation rather than hidden by only reporting
the average.

## What's deliberately out of scope

- **Market orders / stop orders**: only limit orders are implemented.
  Adding a market order is a small extension (match against the book
  with no price limit, i.e. "cross at any price"), left out to keep the
  core matching logic minimal and fully tested.
- **Order modification (price/qty amend)**: currently requires
  cancel + re-submit, matching how many real venues treat a price
  change anyway (it loses time priority).
- **Multiple symbols in one process**: supported by running multiple
  `MatchingEngine` instances, not by making one engine symbol-aware.
