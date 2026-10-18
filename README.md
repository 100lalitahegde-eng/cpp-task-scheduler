# Bounded concurrent task scheduler — C++20

[![Verify](https://github.com/100lalitahegde-eng/cpp-task-scheduler/actions/workflows/verify.yml/badge.svg)](https://github.com/100lalitahegde-eng/cpp-task-scheduler/actions/workflows/verify.yml)

C++20 worker pool with bounded admission, futures, queued cancellation, and explicit shutdown. `submit` returns a task id and future, or throws `QueueFull` or `Stopped`. Accepted work settles that future with the task result, a task exception, or `Cancelled`. `shutdown` closes admission, drains or cancels waiting work, and joins the workers.

## Results

[Verify](https://github.com/100lalitahegde-eng/cpp-task-scheduler/actions/workflows/verify.yml) runs the deterministic, concurrent-race, and lifecycle suites and the batch example on every push and pull request:

| Configuration | Instrumentation |
| --- | --- |
| Release | `CMAKE_BUILD_TYPE=Release` |
| ThreadSanitizer | TSan (`SCHEDULER_THREAD_SANITIZER`) |
| AddressSanitizer | ASan + UBSan (`SCHEDULER_ADDRESS_SANITIZER`) |

GCC and Clang integration jobs build the library, install and relocate the package, and compile an independent consumer through `find_package` and `add_subdirectory`.

Finite-burst medians from the local run on 2026-10-10: four workers, 2,000 attempts, 100 µs requested service, zero-byte payload, three fresh-process repetitions. Figures are the medians in [PERFORMANCE.md](PERFORMANCE.md).

| Workload | Capacity | Accepted | Rejected | Completed/s | Queue p95, ms | Total p95, ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU spin | 32 | 229 | 1,771 | 28,833 | 1.23 | 1.34 |
| CPU spin | 256 | 454 | 1,546 | 32,755 | 8.44 | 8.54 |
| Sleep/blocking | 32 | 204 | 1,796 | 17,306 | 2.15 | 2.33 |
| Sleep/blocking | 256 | 426 | 1,574 | 20,071 | 13.34 | 13.51 |

At capacity 256 those bursts accepted more work and recorded higher queue latency than at capacity 32. Gated-overload memory snapshots are in [PERFORMANCE.md](PERFORMANCE.md).

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
Race tests check futures and execution accounting. Results for a commit are on its
Verify workflow run.

Benchmark targets stay off unless `SCHEDULER_BUILD_BENCHMARKS=ON`. The harness and the
54-row local run are in [benchmarks/README.md](benchmarks/README.md) and
[PERFORMANCE.md](PERFORMANCE.md).

## Limits

- Waiting tasks are bounded by capacity. Running tasks are bounded by the worker count. Bytes captured by a callable are unbounded.
- A running task runs until it returns. Shutdown waits for it and has no deadline.
- The caller owns the scheduler object and the lifetimes of values captured by tasks.
- Durability, retries, priorities, fairness policy, cross-process scheduling, and CPU affinity sit outside this component.
- The latency table above is the recorded finite-burst experiment in PERFORMANCE.md.

## License

[MIT](LICENSE). Copyright (c) 2026 Lalita Hegade.
