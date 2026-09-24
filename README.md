# Concurrent task scheduler (C++20)

A small worker pool with FIFO queued tasks, futures, and cancellation of work that has not started.

```sh
g++ -std=c++20 -Wall -Wextra -Werror -pthread -Iinclude src/scheduler.cpp tests/scheduler_test.cpp -o scheduler_test
./scheduler_test
# Or: cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

## Concurrency contract

The queue, next ID, and stop flag are guarded by one mutex. A worker removes a task under the lock and executes it outside the lock. Cancellation succeeds only while the task remains queued; its future then throws `cancelled`. Task exceptions propagate through futures. Destruction drains queued work and joins workers.

## Evidence and limits

The test holds the sole worker busy, cancels a queued task, and checks that only the running task executes. It also checks error propagation. This is a useful correctness test, not a stress proof. The queue is unbounded; there is no running-task cancellation, priority scheduling, or benchmark. Add a queue capacity policy, a race stress suite with sanitizers, and throughput/latency measurements before presenting it as a production scheduler.
