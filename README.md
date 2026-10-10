# Bounded concurrent task scheduler — C++20

A reusable worker pool demonstrating bounded admission, explicit lifecycle control,
exception-safe worker initialization, queued cancellation, and observable task outcomes.
Designed as a core-systems engineering portfolio project; production suitability must
be evaluated against the integrating application's workload and failure requirements.

## Build and run

Requires CMake 3.16+, a C++20 GCC/Clang toolchain and threads on Linux.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/batch_example
```

The `TaskScheduler::scheduler` CMake target propagates its C++20, include directory
and thread requirements. The existing `scheduler` target remains available.
Tests retain assertions in Release builds and have finite CTest timeouts.
See [ARCHITECTURE.md](ARCHITECTURE.md) for ownership, synchronization and integration boundaries.

## Use in another application

Install a normal Release build:

```sh
cmake --install build --prefix /path/to/scheduler
```

Configure your application with `-DCMAKE_PREFIX_PATH=/path/to/scheduler`, then use:

```cmake
find_package(TaskScheduler 0.4 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE TaskScheduler::scheduler)
```

For source integration, use `add_subdirectory(path/to/cpp-task-scheduler)` and link
the same target. Tests/examples default off when embedded; standalone builds can
disable them with `SCHEDULER_BUILD_TESTS=OFF` and `SCHEDULER_BUILD_EXAMPLES=OFF`.
The library does not change the parent's C++ standard or warning flags.
The installed package supports relocation and resolves its own thread dependency.
It is a static package; rebuild with your application's toolchain and standard library.

```cpp
Scheduler pool(4, 128); // Four workers, at most 128 WAITING tasks.
try {
    auto ticket = pool.submit([] { /* application work */ });
    ticket.result.get(); // Propagates the original task exception, if any.
} catch (const Scheduler::QueueFull&) {
    // Reject, defer upstream, or apply application-specific backpressure.
}
pool.shutdown(); // Stop accepting, drain accepted work, join workers.
```

`examples/batch.cpp` demonstrates bounded batch admission and accounts for rejected
work. Its accepted count varies with scheduling. Rejection never executes the task.
The one-argument constructor remains supported with a default waiting capacity of 1024;
this deliberately replaces the earlier unbounded behavior.

## API and lifecycle contract

| Operation | Behavior |
| --- | --- |
| `submit(work)` | Nonblocking admission decision under the queue mutex; returns an ID/future on acceptance. Throws `QueueFull` or `Stopped` on overload/closed admission. |
| `cancel(id)` | Removes a waiting task and settles its future with typed `Cancelled`; returns false for running, completed, unknown or already-cancelled IDs. |
| `request_stop(drain)` | Closes admission; workers finish accepted tasks. Does not join and is safe from a worker task. |
| `request_stop(cancel_pending)` | Closes admission, cancels all waiting tasks, leaves running tasks alone. Can upgrade an earlier drain request. |
| `shutdown(mode)` | Requests stop and joins workers. Concurrent external callers serialize joins; a cancelling caller can upgrade another caller's drain. Repeat calls are supported. |
| `stats()` | Coherent mutex-protected snapshot of waiting/active counts and cumulative outcomes. |
| Destructor | Drains and joins. All external callers must have finished accessing the object before destruction starts. |

Admission, dequeue, cancellation and stop decisions serialize on one mutex. Once a
stop request takes that mutex, later submissions are rejected. A task racing stop
is either accepted and resolved according to the selected mode, or rejected.
FIFO describes **dequeue order**, not execution start or completion order across workers.
User task execution and callable destruction occur outside the queue mutex.
Cancellation settles the future before returning. IDs are local to one scheduler;
exhaustion throws rather than reusing an ID. Zero workers/capacity and empty tasks
are invalid. Allocation failures can propagate without admitting work.

Workers that were started before a constructor failure are stopped and joined before
that failure is rethrown. A private test factory injects this failure without relying
on exhausting operating-system resources.

`shutdown` from one of this scheduler's workers throws `logic_error` before changing
state, avoiding self-join. Workers should use `request_stop`. Destroying a scheduler
from its own worker is a lifetime contract violation and terminates the process.
Do not block every worker waiting for work queued to the same pool: that can deadlock.

## Resource limits and diagnostics

Waiting task count is bounded by capacity; active task count is bounded by worker
count. This is not a bound on bytes captured by arbitrary user callables, caller-held
futures, concurrent submission temporaries, or operating-system thread resources.
No completed-ticket history is retained by the scheduler. Queued cancellation scans
at most the configured capacity (O(capacity)); queue operations otherwise use a deque.

`accepted = queued + active + completed + failed + cancelled` for a snapshot (before
counter overflow). `completed` counts successful tasks, `failed` counts thrown task
exceptions, and `rejected` counts queue-full/stopped submissions. Invalid arguments,
ID exhaustion and allocation failures are not counted as overload rejections.
Counters are unsigned 64-bit diagnostics, not persistent accounting.

## Verification

CI runs the deterministic, concurrent-race and lifecycle suites plus the example in
Release, ThreadSanitizer, and AddressSanitizer/UndefinedBehaviorSanitizer configurations.
These jobs run on pushes and pull requests and fail on sanitizer findings.
GCC and Clang integration jobs additionally build/test the runtime, install and
relocate the package, and build/run an independent consumer through both
`find_package` and `add_subdirectory`.

```sh
cmake -S . -B build-tsan -DSCHEDULER_THREAD_SANITIZER=ON
cmake --build build-tsan && ctest --test-dir build-tsan --output-on-failure
cmake -S . -B build-asan -DSCHEDULER_ADDRESS_SANITIZER=ON
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

Coverage includes concurrent producers/cancellation, exception propagation, repeated
construction/destruction, overload and capacity reuse, partial construction failure,
drain/cancel-pending shutdown, concurrent joining, worker-initiated stop, and submission
racing shutdown. Gated tests establish required states without sleep-based timing.
Race tests check futures and execution accounting; passing runs cover exercised
schedules, not every possible interleaving. See Actions for results of a specific commit.

## Operating limits

An optional [benchmark harness](benchmarks/README.md) measures finite CPU/blocking
bursts and gated overload memory. [PERFORMANCE.md](PERFORMANCE.md) records a local
54-measurement experiment, raw results and limits; benchmark targets default off.

Running tasks must terminate voluntarily. Shutdown has no deadline and cannot forcibly
interrupt arbitrary C++ code; a stuck task can block it indefinitely. Callers own object
lifetime and task-capture lifetimes. The scheduler provides no durability, retries,
priorities, fairness guarantee, cross-process scheduling, or CPU affinity.
It has no verified production deployment or workload-specific capacity guarantee.
Before adoption, measure the real workload's queue delay, task latency and memory under
its intended worker/capacity settings, and choose the upstream overload policy explicitly.
