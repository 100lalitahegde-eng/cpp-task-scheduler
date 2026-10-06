# Concurrent task scheduler (C++20)

A small worker pool with FIFO queued tasks, futures, and cancellation of work that has not started.

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
# On a supported GCC/Clang platform, also run the suite with ThreadSanitizer:
cmake -S . -B build-tsan -DSCHEDULER_THREAD_SANITIZER=ON
cmake --build build-tsan && ctest --test-dir build-tsan --output-on-failure
```

## Concurrency contract

The queue, next ID, and stop flag are guarded by one mutex. A worker removes a task under the lock and executes it outside the lock. Cancellation succeeds only while the task remains queued; its future then throws `cancelled`. Task exceptions propagate through futures. Destruction drains queued work and joins workers.

## Evidence and limits

The deterministic test holds the sole worker busy, cancels a queued task, and checks exception propagation. The bounded stress suite exercises four concurrent producers racing workers to dequeue and cancel tasks, checks each future's result and execution count, verifies queued cancellation and drain on shutdown, and repeats pool construction and destruction. The Verify workflow runs both tests normally and with ThreadSanitizer. Passing runs provide race-focused evidence for these paths, not a proof of freedom from races under every schedule.

The queue remains unbounded; there is no running-task cancellation, priority scheduling, or benchmark. Shutdown must not race external calls to `submit` or `cancel`: callers must finish accessing the scheduler before destroying it. Establish a queue capacity policy and measure throughput and latency under a specified workload before presenting this as a production scheduler.
