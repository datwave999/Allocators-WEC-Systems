# Problems with a Shared Queue:

- If one thread writes to a normal queue while another reads from it, they can access its internal state at the same time without synchronization. This can cause a **data race** and undefined behavior.
- Protecting the queue with a mutex makes these accesses safe, but a thread may have to wait for the lock. Contention and scheduling can affect throughput and latency.
- A queue that grows while processing messages may need additional memory allocations. A bounded queue lets us prepare its storage before processing starts.
- Even separate variables can affect each other's performance if they share a CPU cache line. This is called **false sharing**.

# The Fix (SPSC Ring Buffer):

The implementation is in `include/SPSCQueue.h`. **SPSC** means **Single-Producer Single-Consumer**: exactly one thread pushes pointers, and exactly one other thread pops them.

- **Fixed-Size Pointer Array:**
    - The Fix: Allocate an array of `T*` pointers once in the constructor using `new T*[slotCount]{0}`. The pointers initially contain `nullptr`.
    - The Result: `Push()` and `Pop()` use existing slots without allocating memory. The array cannot resize, and the destructor releases it with `delete[]`.

- **Circular Positions:**    - The Fix: Keep two positions, `back` and `front`. When a position reaches the end of the array, wrap it back to zero.
    - The Result: Slots can be reused continuously without shifting any pointers.

- **Atomic Progress Updates:**
    - The Fix: Use `std::atomic<std::size_t>` for both positions. The producer writes `back`; the consumer writes `front`. Each thread also reads the other position.
    - The Result: The threads communicate their progress without a mutex, semaphore, or condition variable. A compile-time check requires these atomic indices to be lock-free on the target platform.

- **Separate Cache Lines:**
    - The Fix: Declare `back` and `front` with `alignas(64)` so their addresses are aligned to separate assumed 64-byte cache lines.
    - The Result: Updating one position does not invalidate the cache line containing the other position. The threads still exchange cache data when reading each other's progress.

# Understanding the Storage:

```cpp
SPSCQueue<int> queue(1000);
// SPSCQueue<int> queue = 8; // Not allowed: automatic conversion.
```
- `T` is `int`, so this queue stores `int*` pointers in a buffer array
- The constructor rounds up the size to the next power of 2. 
- So, the request above creates **1024 physical slots**, but `Capacity()` returns **1023**. One slot always stays unused so the implementation can distinguish a full queue from an empty queue
- `explicit` on the constructor means the caller must deliberately construct the queue:
## Wrapping Around the Array

The implementation calculates the next position using:
`(currentPosition + 1) & (slotCount - 1)`
Because `slotCount` is a power of two, this expression wraps an index in the same way as:
`(currentPosition + 1) % slotCount`
# How Push and Pop Work:
## Producer: `Push(T* pointer)`
1. Read the current `back` position.
2. Calculate the next position, wrapping around if necessary.
3. If the next position equals `front`, return `false`: no free slot is currently observed.
4. Write the supplied pointer into `buffer[currentBack]`.
5. Publish the new `back` position and return `true`.

The pointer is written **before** the producer announces that it is available. On failure, neither the buffer nor `back` changes. The producer can retry with the same pointer.

## Consumer: `Pop(T*& pointer)`
1. Read the current `front` position.
2. If it equals `back`, return `false`: no queued pointer is currently observed.
3. Copy the pointer from `buffer[currentFront]` into the caller's variable.
4. Publish the next `front` position and return `true`.

`T*&` means a **reference to a pointer**. It lets `Pop()` change the caller's pointer variable. A failed pop leaves that variable unchanged.
Advancing `front` makes the slot available for another pointer.

# Why the Memory Ordering Matters:

Atomic indices alone are not enough. Their ordering must also protect the ordinary pointer array and the payload writes made before publication.

| Atomic operation | Ordering | Purpose |
| --- | --- | --- |
| Producer reads `back` | `relaxed` | Read its own position; only the producer changes it |
| Producer reads `front` | `acquire` | Observe freed slots after the consumer has copied their pointers |
| Producer writes `back` | `release` | Publish the pointer and earlier payload initialization |
| Consumer reads `front` | `relaxed` | Read its own position; only the consumer changes it |
| Consumer reads `back` | `acquire` | Observe published pointers and initialized payloads |
| Consumer writes `front` | `release` | Publish that the slot has been read and can be reused |
- **Producer to consumer:** Write the payload and pointer, release `back`, then let the consumer acquire `back` before reading.
- **Consumer to producer:** Copy the pointer out, release `front`, then let the producer acquire `front` before reusing the slot.

# Using the Queue:

```cpp
SPSCQueue<int> queue(8);
int value = 42;

bool pushed = queue.Push(&value);

int* received = nullptr;
bool popped = queue.Pop(received);

if (popped) {
    // *received is 42. The queue has transferred a pointer to value.
}
```

This is a single-thread example. In the pipeline, one producer thread calls `Push()` and one consumer thread calls `Pop()`.

# Testing Phase 2 Implementation:

The tests in `tests/test_spsc.cpp` are called by `RunSPSCTests()`. They follow the arena test runner's pass/fail format and return the number of failed tests. `main()` runs both test suites before the arena benchmarks and returns exit code 1 if either suite fails. These tests have been added but have not been built or run yet.

| Function | Checks |
| --- | --- |
| `Construction()` | Requests round up correctly, including zero and one; usable capacity leaves one slot unused; an overflowing size throws `std::length_error` |
| `Empty()` | Pop returns false when the queue starts empty |
| `SinglePointer()` | Push and pop preserve the exact pointer and value; the queue is empty afterward |
| `FIFOAndWraparound()` | A four-slot queue fills and drains in FIFO order across 10 cycles, checking full and empty behavior while the positions wrap around |
| `Concurrent()` | One thread pushes pointers to integers 0 through 999; another checks that each pointer and integer arrives in order using an eight-slot queue |

The producer writes each integer before pushing its address. The array stays alive until both threads finish. A 15-second deadline stops retry loops if transfer fails to complete. Only the consumer changes the result flag; the main thread reads it after joining the consumer. This test checks integer transfer and ordering for this workload.

### Test Results:
![[Pasted image 20261005193749.png]]

### Limitations

- **Exactly one producer and one consumer are supported.** Multiple callers on either side require a different design.
- **Push and pop do not wait.** The caller decides whether to retry, do other work, or stop. A failed attempt may observe older progress and succeed on a later retry.
- **Construction uses the standard heap.** The pointer array is allocated during setup; push and pop perform no allocations.
- **Payload lifetime is the caller's responsibility.** Destroying the queue releases its slots, not the payload objects.
- **The cache layout assumes 64-byte lines.** Other targets may need a different alignment.
- **Lock-free does not guarantee a fixed elapsed time.** CPU scheduling, cache traffic, and the rest of the workload still affect performance.
