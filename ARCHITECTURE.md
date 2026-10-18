# Architecture and integration contract

## Components

| Boundary | Responsibility |
| --- | --- |
| Public header, `include/scheduler.hpp` | Admission, tickets, typed rejection/cancellation, stop modes and statistics. |
| Runtime, `src/scheduler.cpp` | Own workers, serialize queue transitions, resolve accepted futures and join on shutdown. |
| Application | Own callable/capture lifetimes, choose capacity and overload policy, ensure running tasks finish. |
| CMake package | Export `TaskScheduler::scheduler`, C++20 and thread requirements without exporting warning policy. |
| Tests | Check lifecycle/race contracts and build a separate application using both integration paths. |

The scheduler is a single in-process execution component. Extra queues, transport,
persistence and retry policy belong outside it and require their own contracts.
There is no plugin or abstract executor hierarchy: one implementation currently
serves the documented use case.

## Ownership and synchronization

The scheduler owns a fixed worker vector and a bounded deque of waiting tasks.
A task owns its callable and promise; the caller owns its returned future.
Workers take a shared task reference before dropping the queue lock.
No completed-task registry is retained. A cancelled waiting task is released
outside the mutex so application capture destructors can safely call diagnostics.

`mutex_` serializes admission, dequeue, cancellation, stop state, and counters.
`join_mutex_` serializes external joins. Stop requests do not wait for the join
lock, so cancel-pending can upgrade a concurrent drain. Workers execute user code
without either lock. Running tasks cannot be cancelled by this library.

Partial worker construction stops and joins already-created workers before
rethrowing. Shutdown called by a worker is rejected before any state change;
worker code can instead request stop without joining.

## Reuse and compatibility

Use `add_subdirectory` for source integration or `find_package(TaskScheduler
0.4 CONFIG REQUIRED)` for an installed package. Both expose the same target.
Tests and examples default off when embedded and on when built standalone.
The package is a static library with one public header; shared-library ABI
compatibility is not promised. Rebuild consumers when changing toolchain,
standard library, sanitizer settings or library version.

The 0.x package version policy accepts patch updates within the same minor
version. Source API and runtime behavior are unchanged by this packaging change.
Warning-as-error flags remain private. Sanitizer runtime link flags propagate
only when explicitly enabled; install a normal build for normal consumers.

## Verification

CI separates runtime tests (Release, TSan, ASan/UBSan) from GCC/Clang integration
checks. Integration installs then relocates the package before building a
separate application; it also checks embedded defaults and warning isolation.
The workflow is `.github/workflows/verify.yml`, and it runs on Linux with GCC and Clang.
