
# Arena Allocator Benchmarks

### Debug x64 Configuration:

![[Pasted image 20261005123134.png]]

### Release x64 Configuration:

![[Pasted image 20261005123210.png]]

### Why Debug and Release results differ

Debug builds normally use less optimization so the code is easier to step through while debugging. This can leave more function calls and extra work in the benchmark loops. Some library checks can also add overhead. Release builds simplify the code to make it faster. Use Release results when judging the allocator's performance.

This project's Release configuration enables whole-program optimization. The compiler can put the code from `Alloc()` directly into the allocation loop, saving a function call. It can also simplify calculations when an argument is known. With alignment fixed at 1, every address is already aligned, so the padding calculation can disappear. This helps explain why the Release allocation results are much lower, especially for 1-byte alignment. The generated machine code would need to be inspected to confirm exactly what was simplified.

Creation times can stay similar between Debug and Release because much of the work is Windows allocating memory and preparing its pages. Compiler optimization has less influence on that work. Small differences between runs do not establish that one configuration creates arenas faster.

### Limitations

- **The measurements include benchmark overhead.** Allocation timing includes the loop and storing each returned pointer in a volatile variable. Reset timing includes the loop, accessing each arena, and an indirect function call. These numbers include more than the allocator's pointer updates.
- **Reading the clock takes time.** Timing many operations together spreads that cost across the batch, but it does not remove it. The reset batches are especially short, so clock overhead can still matter.
- **The numbers are averages across batches.** A result below 1 ns describes the average cost across many operations. It does not mean every individual call has that exact latency.
- **Allocation only reserves space inside the arena.** The benchmark does not write or read the requested memory. Its results describe pointer allocation, with arena creation and reset outside the timer. Creation timing includes both `VirtualAlloc` and pre-faulting, and excludes destruction.
- **Optimization guards have limits.** Volatile pointer stores keep results observable, and the volatile function pointer guards the reset calls. They still allow other compiler optimizations. The results describe this particular compiled workload.
- **Results vary with the machine and the run.** CPU speed, caches, background activity, compiler settings, and benchmark order can affect timings. Repeat the program under similar conditions before drawing conclusions from small differences between sizes or alignments.
