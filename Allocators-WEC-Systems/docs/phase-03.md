# Problems When Combining the Allocator and Queue:

- The arena provides memory and the SPSC queue transfers pointers, but neither decides when the consumer has finished using a packet. The pipeline must manage that lifetime.
- **An empty queue does not mean processing is finished.** `Pop()` copies the pointer and releases its queue slot before the consumer reads the packet. Resetting the arena at this point could let the producer overwrite data that the consumer is still reading.
- The arena's bump pointer is not atomic. Calling `Alloc()` or `Reset()` from both threads would introduce unsynchronized access to allocator state.
- A full queue or an empty queue requires a waiting policy. Repeatedly handing execution back to the OS with `yield()` can introduce scheduling delays into the processing loop.

# The Fix (Producer-Consumer Pipeline):

The implementation is in `include/Pipeline.h` and `src/Pipeline.cpp`. It combines the Phase 1 arena with the Phase 2 SPSC queue to simulate incoming packet metadata.

- **Producer-Owned Arena:**
    - The Fix: Only the producer calls `Alloc()` and `Reset()`. The consumer reads packet data without accessing the allocator's state.
    - The Result: Allocation remains a simple bump-pointer operation without adding a mutex to the arena.

- **Pointer-Based Message Transfer:**
    - The Fix: Initialize each packet in arena storage, then push its address onto `SPSCQueue<PacketMetadata>`.
    - The Result: The queue transfers pointers rather than copying whole packet objects. Its release/acquire ordering makes the initialized payload available to the consumer.

- **Explicit Batch Completion:**
    - The Fix: After reading every packet in an arena batch, the consumer publishes a completed-batch count. The producer waits for that acknowledgement before recycling memory.
    - The Result: The producer cannot overwrite an earlier batch while the consumer is still using it.

- **O(1) Arena Recycling:**
    - The Fix: After observing completion, the producer calls `arena.Reset()` once for the whole batch.
    - The Result: The same memory is reused for the next batch without individually freeing packets. Reset moves the bump pointer; the OS allocation remains alive until the arena is destroyed.

- **CPU Spin-Waiting:**
    - The Fix: Use `_mm_pause()` when a push, pop, or completion check needs to be retried.
    - The Result: The retry loop uses an x86 CPU spin-wait hint without voluntarily yielding execution to the OS scheduler. Normal OS preemption can still occur.

# Pipeline Flow:

`PacketMetadata` contains a sequence number, payload value, sender identifier, and flags. The payload is `sequence + 1`, making its expected checksum easy to calculate.

The defaults are 128 batches of 65,536 packets, a 2 MiB arena, and 4,095 usable queue slots. The arena holds one batch at a time. The queue can be smaller because the producer and consumer run concurrently, so slots are reused while that batch is being transferred.

**Batches define when memory can be recycled.** The consumer still pops and processes one packet at a time; there is no bulk queue operation.

## Producer

1. `AllocatePacket()` requests `sizeof(PacketMetadata)` bytes with `alignof(PacketMetadata)` alignment.
2. `std::construct_at()` initializes the packet in that arena storage, without another heap allocation.
3. `ProduceBatch()` pushes its pointer. If the queue is full, it retries the **same packet** using `_mm_pause()` rather than allocating another one.

Returning `true` from `ProduceBatch()` means all pointers were published, but the consumer may still be reading them. `RunProducer()` therefore waits for completion before resetting the arena and generating the next batch. Sequence numbers continue across batches.

## Consumer

1. `ConsumeBatch()` polls the queue, using `_mm_pause()` while it is empty.
2. It checks each pointer and verifies that the packet's sequence matches the expected number.
3. It adds `packet->value` to the checksum and increments the processed-message count.

`ConsumerStats` stores these totals across all batches. Only the consumer writes them, and the runner reads them after joining the threads, so the statistics do not need to be atomic. Arena packets are not individually deleted.

## Completion and Recycling

`PipelineControl` contains two atomic variables on separate cache lines: `completedBatches` and `stopRequested`.

After all reads in a batch are finished, `RunConsumer()` stores the completed-batch number with `release` ordering. The first batch publishes 1, the next publishes 2, and so on. `RunProducer()` waits until an `acquire` load observes the required number, then calls `arena.Reset()`. This ensures completed payload reads happen before the producer overwrites that storage.

| Communication | Ordering | Purpose |
| --- | --- | --- |
| Queue publication | Producer release, consumer acquire | Make initialized packets available for reading |
| Batch completion | Consumer release, producer acquire | Make arena storage safe to reuse |
| Stop request | `relaxed` | Communicate cancellation without publishing other data |

If allocation fails or a packet's sequence is wrong, a worker sets `stopRequested`. Both workers check it in their retry loops so they can exit rather than wait indefinitely. Cancellation does not authorize a reset; shared memory stays alive until both workers stop.

# Running and Checking the Pipeline:

`RunPipeline(config, stats)` validates the configuration, creates the arena and queue, and starts one producer and one consumer. It joins both threads before reading their results or releasing shared memory.

The run succeeds when both workers return successfully and these checks pass:

- **Sequence:** Packets arrive in order, checked by the consumer as it processes them.
- **Count:** `messagesProcessed` equals `batchCount * messagesPerBatch`.
- **Checksum:** For `N` packets with payloads 1 through `N`, the total equals `N * (N + 1) / 2`.

# Basic Pipeline:

The alternative implementation is in `include/BasicPipeline.h`. `MutexQueue` wraps a bounded `std::queue<PacketMetadata*>` with `std::mutex` and uses the same effective capacity as the SPSC queue. The producer creates packets with `new`; the consumer checks their sequence, accumulates the same statistics, and calls `delete`. The mutex protects queue operations only, with allocation and processing outside the lock. The workers reuse the same configuration and batch acknowledgement, publishing completion after the batch's packets have been deleted. Unpublished packets are protected by a temporary `std::unique_ptr`, and the queue deletes leftover packets after both workers stop.

### Limitations

- **Exactly one producer and one consumer are used.** The custom pipeline relies on the SPSC queue's ownership rules.
- **Workers wait at arena boundaries.** The producer cannot allocate the next batch until the consumer acknowledges the current one.
- **Spin-waiting consumes CPU resources.** `_mm_pause()` does not prevent OS scheduling delays or guarantee a fixed processing time.
- **The payload is simple.** Adding members that own resources would require appropriate destruction before resetting their storage; `Reset()` does not call object destructors.
- **The current platform is Windows on x86/x64.** The arena uses `VirtualAlloc`, and polling uses the x86 `_mm_pause()` intrinsic.
