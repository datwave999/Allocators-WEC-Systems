# Problems with malloc() or new:

- When malloc() gives you memory, the physical RAM isn't always assigned immediately. The OS manages memory in chunks called pages (usually 4KB). Often, physical RAM is only assigned when you actually try to use that memory, which triggers a page fault, a slow process where the kernel pauses your program to wire up a 4KB page of physical memory. The initial allocation is also slow because malloc has to traverse Linked Lists to find free memory slots.
- If malloc needs more memory than what is currently mapped to your program's virtual memory space, it has to wake up the kernel (via a system call). This is very slow and forces the OS to increase your virtual memory pool.
- malloc also wastes space for bookkeeping. Because it has to know exactly how much memory to delete later, it stores metadata (a header) right in front of the memory it gives you. On top of that, it rounds up the size you asked for to standard block sizes (like 16 or 32 bytes) to keep memory aligned. This wasted space inside the allocated block is called **Internal Fragmentation**.
- Also, for example, if malloc manages a 64MB pool and half of the data is freed over time, leaving 32MB of free space. If we then ask for 32MB of contiguous data, it might not be able to give it to us because that free space is chopped up into little holes scattered between active data. This is called **External Fragmentation**.
- Because the standard heap (virtual memory space shared by the entire process) is a global  shared by all threads in your program, malloc() and free() must use mutexes internally to prevent two threads from grabbing the exact same piece of memory at the exact same time. IF two threads try to grab the exact same memory, the OS forces one to wait, and you don't just lose time waiting for the lock, you also suffer a context switch, which wipes out CPU caches and forces your thread to fetch data from main memory again.

# The Fix (Bump Allocator): 
- **Pre-Allocation & Pre-Faulting:**
    - The Fix: We ask Windows for a massive block of memory upfront using VirtualAlloc. We instantly run a loop that writes one byte every 4096 bytes (the hardware page size).
    - The Result: We force the hardware to map 100% of the physical RAM. The OS is never disturbed.

- **The Bump Pointer:**
    - The Fix: We treat the 1GB block as a blank canvas. We maintain a single bump_pointer. When a user requests N bytes, we simply do bump_pointer += N. There are no hidden headers, no linked lists to search, and no wasted space.
    - The Result: Allocation goes from taking hundreds of CPU cycles to exactly 1 cycle (basic addition). Allocator induced Internal Fragmentation is avoided.

- **Bitwise Alignment Math (Fixes Hardware Penalties):**
    - The Fix: Unaligned data causes hardware penalties or crashes. Instead of using slow modulo division (%) to align our pointer, we use the bitwise formula: Aligned = (Pointer + Alignment - 1) & ~(Alignment - 1).
    - The Result: Perfect power-of-two alignment calculated in 2 CPU cycles.

- **O(1) Batch Reset (Fixes the "Free" problem):**
    - The Fix: We don't call free() on individual objects. When all the data is completely processed, we just set bump_pointer = start_pointer.
    - The Result: Deallocating does not cause External Fragmentation

# Testing Phase 1 Implementation:

## Automated Tests

The tests in `tests/test_arena.cpp` are called by `RunArenaTests()`. Each test returns `true` on success or prints a failure reason and returns `false`. The runner reports pass/fail results, catches standard exceptions from each test, and returns the number of failed tests. `main()` returns exit code 0 when all tests pass or 1 when any test fails.

| Function                   | Checks                                                                                                                                                                                                                                     |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `InitialStateAndReset()`   | A 64 KiB arena starts empty; allocating 16 bytes reports 16 used; reset clears used space; another 16-byte allocation returns the same address and reports 16 used; repeated reset leaves the arena empty                                  |
| `ReadWrite()`              | Five integers can be allocated, written, and read back correctly; a second five-integer allocation succeeds without overlapping or moving behind the first; total used space equals the size of both arrays                                |
| `Alignment()`              | After allocating one byte, an eight-byte allocation succeeds with each alignment in `{1, 2, 4, 8, 16, 32, 64}` and returns a divisible address; alignment 3 is rejected without changing used space                                        |
| `Capacity()`               | Allocating the full 64 KiB capacity succeeds and reports the arena as full; another byte is rejected; in a fresh arena with eight bytes used, requesting one byte more than the remaining capacity is rejected without changing used space |
| `PaddingExceedsCapacity()` | A 16-byte arena accepts a 15-byte allocation; its base is checked for 32-byte alignment; a one-byte request with alignment 32 is rejected because the required 17 padding bytes exceed the one byte remaining; used space stays unchanged  |
**Results**:

![[Pasted image 20261004011327.png]]

## Manual Memory Usage Experiment

- Created an Arena Allocator of 2GB
- Allocated an array of 5 integers in it
- Performed Read and Write operations on it
- Checked size of Allocation before and after Reset
- Confirmed that 2GB ram is "Pre-Faulted" using Task Manager

![[Pasted image 20261003222142.png]]
