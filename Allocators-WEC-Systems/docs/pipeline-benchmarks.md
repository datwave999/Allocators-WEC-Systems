# Pipeline Benchmarks

## Workload and Timing

Both pipelines use one producer and one consumer to process 128 batches of 65,536 packets: **8,388,608 messages** in total. They generate the same 32-byte `PacketMetadata` objects, check the same sequence numbers, and accumulate the same checksum. Both queues have a maximum of 4,095 queued pointers.

- **Arena + SPSC:** Allocate packets from a pre-faulted 2 MiB arena, transfer their pointers through the SPSC queue, and reset the arena after each batch is acknowledged.
- **Basic pipeline:** Allocate each packet with `new`, transfer its pointer through a mutex-protected `std::queue`, and call `delete` after processing. It uses the same batch acknowledgement and `_mm_pause()` retry policy.

The custom implementation is measured by `RunTimedPipeline()` in `benchmarks/benchmark_pipeline.cpp`. The basic implementation is measured by `RunTimedBaselinePipeline()` in `include/BasicPipeline.h`.

1. Create the resources and both worker threads before timing. Each worker waits at a shared start gate.
2. Once both workers are ready, record the start time with `std::chrono::steady_clock` and release the gate.
3. Record the stop time inside the producer after it observes the final batch acknowledgement. This includes the final arena reset for the custom pipeline, or completion of all packet deletions for the basic pipeline.
4. Join both workers, validate the results, and calculate throughput.

The timed interval includes allocation, initialization, queue operations, sequence checks, checksum accumulation, retries, batch waits, and memory recycling. Resource setup, thread creation, joins, final validation, and printing are outside the timer. Any additional storage allocations performed by `std::queue` during processing are included.

```text
Throughput = Messages processed / Elapsed seconds
```

## Release x64 Results

The following values are from the runs shown in the screenshots below:

| Pipeline | Elapsed time | Messages per second |
| --- | --- | --- |
| Arena allocator + SPSC queue | 0.151415 s | 55,401,580 |
| `new/delete` + mutex-backed queue | 0.651793 s | 12,870,047 |

Both runs reported **Validation: PASS**, processed 8,388,608 messages, and produced the expected checksum of **35,184,376,283,136**.

The custom pipeline achieved approximately **4.30 times the throughput** and used **76.8% less elapsed time** for this workload.

### Arena Allocator + SPSC Queue

![[Pasted image 20261005231759.png]]

### Basic Pipeline Using new/delete and Mutex

![[Pasted image 20261005231828.png]]

## Why the Implementations Differ

The custom pipeline uses bump-pointer allocation and reuses its arena across batches. Its queue storage is allocated upfront, and pointer transfer does not acquire a mutex or allocate more queue storage.

The basic pipeline performs a heap allocation and deletion for every packet. Each push and pop also acquires a mutex, serializing access to the queue. These additional operations help explain the observed difference, but the measurements do not establish how much time each individual operation costs.

## Limitations

- **These are individual runs.** The screenshots do not establish an average, median, or variation across repeated measurements.
- **Results depend on the workload and machine.** CPU scheduling, background activity, caches, compiler settings, and execution order can affect the result. The current program runs the custom pipeline before the basic pipeline.
- **The comparison changes both allocation and queue design.** It measures their combined effect rather than isolating allocator savings from synchronization savings.
- **Throughput is not individual message latency.** These results do not establish a per-packet latency distribution or a fixed processing-time guarantee.
