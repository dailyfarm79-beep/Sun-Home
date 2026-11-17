#!/usr/bin/env python3

import argparse
import concurrent.futures
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
sys.path.insert(0, str(TESTS))
from test_sun import SunCompilerTests


def run_shard(test_names, worker_dir):
    env = os.environ.copy()
    env["SUN_TEST_WORK"] = str(worker_dir)
    names = [f"test_sun.SunCompilerTests.{name}" for name in test_names]
    return subprocess.run(
        [sys.executable, "-m", "unittest", "-q", *names],
        cwd=TESTS,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )


def main():
    parser = argparse.ArgumentParser(description="Run Sun tests in isolated workers.")
    parser.add_argument(
        "--jobs",
        default="auto",
        help="parallel worker count, or 'auto' (default: up to 4 workers)",
    )
    args = parser.parse_args()
    test_names = unittest.defaultTestLoader.getTestCaseNames(SunCompilerTests)
    if args.jobs == "auto":
        jobs = min(4, os.cpu_count() or 1)
    else:
        try:
            jobs = int(args.jobs)
        except ValueError:
            parser.error("--jobs must be a positive integer or 'auto'")
        if jobs < 1:
            parser.error("--jobs must be a positive integer or 'auto'")
    jobs = min(jobs, len(test_names))

    shards = [[] for _ in range(jobs)]
    for index, name in enumerate(test_names):
        shards[index % jobs].append(name)

    build_dir = ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    work_root = pathlib.Path(tempfile.mkdtemp(prefix="test-sun-", dir=build_dir))
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
            futures = [
                executor.submit(run_shard, shard, work_root / f"worker-{index}")
                for index, shard in enumerate(shards)
            ]
            results = [future.result() for future in futures]

        for index, result in enumerate(results):
            if result.stdout:
                sys.stdout.write(result.stdout)
            if result.stderr:
                sys.stderr.write(result.stderr)
            if result.returncode:
                print(f"Test worker {index + 1}/{jobs} failed.", file=sys.stderr)
                return result.returncode
        print(f"All {len(test_names)} tests passed across {jobs} workers.")
        return 0
    finally:
        shutil.rmtree(work_root)


if __name__ == "__main__":
    raise SystemExit(main())
