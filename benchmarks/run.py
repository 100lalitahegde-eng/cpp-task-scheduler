#!/usr/bin/env python3
"""Run each synthetic case in a fresh process; save raw CSV and environment."""
import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--build-command", required=True)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    if not 1 <= args.repeats <= 100:
        parser.error("--repeats must be between 1 and 100")
    args.output.mkdir(parents=True, exist_ok=True)
    cases = []
    for mode in ("cpu", "blocking"):
        for workers in (1, 2, 4):
            for capacity in (32, 256):
                cases.append((mode, workers, capacity, 2000, 100, 0))
    for capacity in (32, 256):
        for payload in (0, 4096, 65536):
            cases.append(("overload", 2, capacity, capacity * 4, 0, payload))
    rows = []
    for case in cases:
        for repeat in range(1, args.repeats + 1):
            result = subprocess.run([str(args.binary.resolve()), *map(str, case)],
                                    check=True, capture_output=True, text=True, timeout=60)
            parsed = list(csv.DictReader(result.stdout.splitlines()))
            if len(parsed) != 1:
                raise RuntimeError("benchmark did not return one CSV row")
            rows.append({"repeat": repeat, **parsed[0]})
    with (args.output / "raw.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    cpu_model = "unknown"
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text().splitlines():
            if line.startswith("model name"):
                cpu_model = line.partition(":")[2].strip()
                break
    affinity = sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None
    metadata = {
        "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "platform": platform.platform(), "cpu_model": cpu_model,
        "logical_cpus": os.cpu_count(), "affinity": affinity,
        "source_revision": args.source_revision, "build_command": args.build_command,
        "binary": str(args.binary.resolve()), "repeats": args.repeats,
        "harness_sha256": hashlib.sha256(
            Path(__file__).with_name("scheduler_benchmark.cpp").read_bytes()).hexdigest(),
        "cases": len(cases), "compiler": subprocess.run(
            [args.compiler, "--version"], check=True, capture_output=True, text=True).stdout.splitlines()[0],
        "limits": "No CPU pinning or warmup. Latency samples cover accepted tasks only. "
                  "RSS includes allocator retention.",
    }
    for name in ("cpu.max", "memory.max"):
        path = Path("/sys/fs/cgroup") / name
        if path.exists():
            metadata[name] = path.read_text().strip()
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Saved {len(rows)} measurements to {args.output}")


if __name__ == "__main__":
    main()
