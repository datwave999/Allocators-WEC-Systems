# Lock-Free Concurrency & Custom Allocators

A C++20 systems programming project that combines a custom arena allocator with a lock-free Single-Producer Single-Consumer (SPSC) queue to process simulated packet metadata. A basic pipeline using `new/delete` and a mutex-backed queue provides a comparison.

## Implementation

| Phase | Component | Features |
| --- | --- | --- |
| 1 | Arena allocator | Windows `VirtualAlloc`, page pre-faulting, power-of-two alignment, no per-allocation metadata, and O(1) reset |
| 2 | SPSC ring buffer | Bounded pointer storage, acquire/release ordering, indices on separate 64-byte cache lines, and no mutex in push/pop |
| 3 | Producer-consumer pipeline | Arena-backed packets, sequence validation, checksum accumulation, and safe memory recycling across batches |

The producer allocates and initializes each packet, then publishes its pointer. The consumer reads packets one at a time and maintains a count and checksum. After finishing a batch, it publishes a completion acknowledgement with release ordering. The producer observes that acknowledgement with acquire ordering before resetting the arena.

This acknowledgement is necessary because an empty queue can still leave the consumer reading its last popped packet. Only the producer changes the arena's allocation state. Retry loops use `_mm_pause()`.

The basic pipeline generates the same packets and uses the same queue capacity and batch protocol. It allocates each packet with `new`, protects `std::queue` operations with `std::mutex`, and deletes packets after processing.

## Build and Run

Requirements: Windows on x86/x64, Visual Studio with the **Desktop development with C++** workload, the **MSVC v145** toolset used by the project, and a Windows SDK. The project is configured for C++20.

1. Open [Allocators-WEC-Systems.slnx](Allocators-WEC-Systems.slnx) in Visual Studio.
2. Select **Release** and **x64** for performance measurements.
3. Build the solution and run without debugging using **Ctrl+F5**.

The program runs arena and SPSC tests first. If they pass, it runs the arena allocator measurements, the arena/SPSC pipeline, and the basic pipeline. It returns 0 on success and 1 if a test, validation, or measurement fails.

Default pipeline settings are in [Pipeline.h](Allocators-WEC-Systems/include/Pipeline.h):

| Setting | Value |
| --- | --- |
| Batches | 128 |
| Messages per batch | 65,536 |
| Total messages | 8,388,608 |
| Packet size | 32 bytes |
| Arena size | 2 MiB, reused each batch |
| Usable queue capacity | 4,095 pointers |

## Recorded Pipeline Results

The documented Release x64 runs produced:

| Pipeline | Elapsed time | Messages per second |
| --- | --- | --- |
| Arena allocator + SPSC queue | 0.151415 s | 55,401,580 |
| `new/delete` + mutex-backed queue | 0.651793 s | 12,870,047 |

Both runs processed 8,388,608 messages, passed validation, and produced the expected checksum of **35,184,376,283,136**. The custom pipeline achieved approximately **4.30 times the throughput** in this comparison.

Timing starts after both workers reach a start gate and ends after the producer observes the final completion acknowledgement, including the custom pipeline's final arena reset. Setup, thread creation, joins, final validation, and printing are excluded. Message allocation, initialization, queue operations, processing, deletion or recycling, and waiting are included.

These are individual runs, not repeated-run statistics. Results depend on the machine and workload, measure the combined allocator and queue designs, and do not establish individual packet latency. See [pipeline measurements](Allocators-WEC-Systems/docs/pipeline-benchmarks.md) for screenshots and details.

## Documentation

- [Phase 1: Arena allocator](Allocators-WEC-Systems/docs/phase-01.md)
- [Phase 2: SPSC queue](Allocators-WEC-Systems/docs/phase-02.md)
- [Phase 3: Integrated pipeline](Allocators-WEC-Systems/docs/phase-03.md)
- [Arena allocator measurements](Allocators-WEC-Systems/docs/arena-benchmarks.md)
- [Pipeline comparison](Allocators-WEC-Systems/docs/pipeline-benchmarks.md)

## Project Layout

```text
Allocators-WEC-Systems.slnx
README.md
Allocators-WEC-Systems/
    include/       Arena, queue, pipeline declarations, and basic pipeline
    src/           Arena and pipeline implementations; program entry point
    tests/         Arena and SPSC test suites
    benchmarks/    Arena and custom pipeline measurements
    docs/          Phase explanations, results, and screenshots
```

## Scope

- The current implementation uses Windows `VirtualAlloc` and the x86 `_mm_pause()` intrinsic.
- The custom queue supports exactly one producer and one consumer. The producer waits for completion at each arena batch boundary.
- Arena reset does not call object destructors. The current packet contains only integers; resource-owning payloads would require appropriate cleanup.
- MPMC support, huge pages, and ThreadSanitizer integration are optional extensions and are not implemented.

## Learning Resources

- [Microsoft’s VirtualAlloc documentation](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc) for Windows memory allocation.
- [Microsoft’s C++ atomic reference](https://learn.microsoft.com/en-us/cpp/standard-library/atomic?view=msvc-170) for atomic operations and memory ordering.
- [C++ memory functions](https://learn.microsoft.com/en-us/cpp/standard-library/memory-functions?view=msvc-170#construct_at) for constructing objects in pre-allocated storage.
- [Fedor Pikus’s “C++ Atomics, From Basic to Advanced”](https://www.youtube.com/watch?v=ZQFzMfHIxng), a CppCon talk on atomics and lock-free concurrency.

### Use of AI

I use AI to learn unfamiliar concepts, figure out how to properly structure the project, and work through the implementation of new features. I don't have AI agents writing all the code for me. I mostly write the code myself. When I do use an AI-generated snippet, it's a short section that I read and understand line by line before using it.