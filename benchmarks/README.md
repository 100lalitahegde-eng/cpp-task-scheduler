# Synthetic workload benchmark

Build a normal optimized binary; sanitizer timings are not performance results.

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DSCHEDULER_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel 2
ctest --test-dir build-bench --output-on-failure
python3 benchmarks/run.py --binary build-bench/scheduler_benchmark \
  --output benchmarks/results/my-run --source-revision "$(git rev-parse HEAD)" \
  --build-command "cmake Release with SCHEDULER_BUILD_BENCHMARKS=ON" --compiler g++
```

The executable can also run one case:

```sh
./build-bench/scheduler_benchmark cpu 4 256 2000 100 0
./build-bench/scheduler_benchmark blocking 4 256 2000 100 0
./build-bench/scheduler_benchmark overload 2 256 1024 0 65536
```

Arguments: workload, workers, waiting capacity, submission attempts, requested
service microseconds, and captured payload bytes per task. Invalid/zero resource
counts fail. An overload case requires attempts >= capacity.

## What is measured

- **cpu:** a single producer submits a finite burst; accepted tasks spin using a
  steady clock for the requested duration. This measures synthetic CPU occupancy,
  including clock overhead, rather than a representative application algorithm.
- **blocking:** the same burst, with sleep-based service. Wakeup latency can exceed
  the request. It approximates waiting work, not real network or disk I/O.
- **overload:** start and gate every worker, fill waiting capacity, then reject the
  remaining attempts. Sample current RSS before and after filling while all workers
  remain gated, then release and verify that every accepted task completed.

The runner runs 18 cases, each three times by default in a fresh process, and writes
raw CSV plus environment/build metadata and the C++ harness SHA-256. Worker
construction is outside burst timing. Timing includes producer submissions,
rejected attempts, draining, and joining. Completed/s = accepted / timed seconds.
It is **finite burst throughput**, not steady-state sustainable arrival rate.

Queue delay is measured from just before capture allocation/submission to task
entry; it includes producer allocation and admission overhead. Total latency ends
at completion of the task body, before promise settlement. p50/p95/p99 use the
nearest-rank percentile of accepted tasks only. Rows also show rejections; a low
latency with many rejected requests is not good end-to-end service performance.
The harness verifies accounting, payload execution, completion, and empty final
state. It stores samples but does not retain futures for the burst.

Linux RSS is read from /proc/self/status. RSS delta includes task structures,
callable payloads, allocator retention (including rejected temporary payloads),
and other process pages. It does not measure just scheduler bytes or maximum
transient RSS, and is not a universal memory bound. Payload vectors initialize
all bytes and are consumed by completed tasks. The overload baseline includes
workers and their running gate tasks, so it excludes worker startup cost.
RSS unavailable values are -1. Overload timing/latency fields are 0 (not measured);
burst saturated RSS fields are -1 (not measured).

No warmup, affinity pinning, competing-executor baseline, arrival-rate control,
multi-producer contention, cancellation latency or long-duration soak is included.
Short runs in a shared environment may vary substantially. Inspect all repetitions
and remeasure on deployment hardware before choosing capacity or setting an SLO.

CI runs small self-checking smoke cases under GCC/Clang and sanitizers. It checks
that the harness works; there are no timing thresholds or CI performance claims.
See [PERFORMANCE.md](../PERFORMANCE.md) for the recorded local experiment.
