# Performance measurements

## Local experiment, 2026-10-10

Runtime source: `b5d8c14c9f4b031fe9cc0827f3a8ad5cbeb1be38`.
The benchmark harness is included with this report; its digest is recorded in
[environment.json](benchmarks/results/local-2026-10-10/environment.json).
[Raw measurements](benchmarks/results/local-2026-10-10/raw.csv) contain all
54 rows: 18 cases with three fresh-process repetitions each.

Linux x86-64, AMD EPYC 7763 host model, GCC 13.3, C++20, -O2 -DNDEBUG.
The shared environment exposed nine affinity CPUs and a cgroup quota of eight
CPU equivalents. No CPU pinning or exclusive-machine control was used.
The full build command and resource limits are in the environment record.

The tables show medians across three runs, including medians of each run's p95,
for short synthetic bursts.

## Queue capacity tradeoff

Single producer, 2,000 attempts, four workers, 100 microseconds requested service,
zero-byte captured payload:

| Workload | Capacity | Accepted | Rejected | Completed/s | Queue p95, ms | Total p95, ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU spin | 32 | 229 | 1,771 | 28,833 | 1.23 | 1.34 |
| CPU spin | 256 | 454 | 1,546 | 32,755 | 8.44 | 8.54 |
| Sleep/blocking | 32 | 204 | 1,796 | 17,306 | 2.15 | 2.33 |
| Sleep/blocking | 256 | 426 | 1,574 | 20,071 | 13.34 | 13.51 |

In these bursts, increasing capacity admitted more work and increased waiting
latency. Most attempts were rejected in every row.

## Memory while waiting queue is full

Two workers are held busy with gate tasks. Each row submits four times the
waiting capacity. In all repetitions, accepted = capacity and rejected =
three times capacity, with no execution of rejected work.

| Capacity | Payload/task | Accepted | Rejected | Median RSS delta, KiB |
| --- | ---: | ---: | ---: | ---: |
| 32 | 0 | 32 | 96 | 72 |
| 32 | 4 KiB | 32 | 96 | 208 |
| 32 | 64 KiB | 32 | 96 | 2,184 |
| 256 | 0 | 256 | 768 | 128 |
| 256 | 4 KiB | 256 | 768 | 1,160 |
| 256 | 64 KiB | 256 | 768 | 16,576 |

Waiting count was bounded; captured payload size directly affected resident
memory. Capacity 256 with 64 KiB captures retained 16 MiB of payload alone.
The RSS deltas additionally include allocator and task overhead and retained
rejection allocations. Each sample is taken while the waiting queue is full
and both workers are gated.
