#!/usr/bin/env python3
"""Verify a .perfetto-trace file by querying it with trace_processor.

Lightweight alternative to scripts/verify-trace-playwright.mjs: same
assertions (slice names, duration bounds, debug annotations) plus
--category and --counter checks, run through the trace_processor binary
instead of a browser-hosted WASM instance.

Binary resolution order:
  1. TRACE_PROCESSOR_BIN env var
  2. <repo>/.tools/trace_processor  (fetch: curl -sL https://get.perfetto.dev/trace_processor -o .tools/trace_processor)
  3. trace_processor on PATH

Usage:
  python3 scripts/verify-trace-sql.py <trace-file> \
    --event <name-substring> [--arg <key=value>] [--category <name>] \
    [--min-dur-ms <ms>] [--max-dur-ms <ms>] [--counter <name-substring=value>]

Notes:
  - --arg/--category/--min-dur-ms/--max-dur-ms apply to every --event.
    To assert different args per event, invoke the script once per event.
  - Debug annotation keys appear in the args table as "debug.<key>"; both
    the raw and prefixed form are matched.
  - --counter verifies a sample exists on a counter_track whose name
    matches, with an approximately equal value.
"""

from __future__ import annotations

import argparse
import csv
import io
import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EVENTS = ["withRecording-demo"]


def find_trace_processor() -> str:
    candidates = [
        os.environ.get("TRACE_PROCESSOR_BIN"),
        os.path.join(REPO_ROOT, ".tools", "trace_processor"),
        shutil.which("trace_processor"),
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    raise SystemExit(
        "trace_processor binary not found. Fetch it with:\n"
        "  mkdir -p .tools && curl -sL https://get.perfetto.dev/trace_processor "
        "-o .tools/trace_processor && chmod +x .tools/trace_processor"
    )


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def run_query(tp: str, trace: str, sql: str) -> float:
    result = subprocess.run(
        [tp, "query", trace, sql],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(
            f"trace_processor query failed:\n{result.stderr or result.stdout}"
        )

    rows = list(csv.reader(io.StringIO(result.stdout)))
    if len(rows) < 2 or not rows[1]:
        return 0.0
    try:
        return float(rows[1][0])
    except ValueError:
        raise SystemExit(f"unexpected query output: {result.stdout!r}")


def parse_arg_filters(values: list[str]) -> list[tuple[str, str]]:
    filters = []
    for value in values:
        if "=" not in value or value.startswith("="):
            raise SystemExit(f"Invalid --arg value: {value!r}. Expected <key>=<value>.")
        key, _, arg_value = value.partition("=")
        filters.append((key, arg_value))
    return filters


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "trace",
        nargs="?",
        default=None,
        help="Path to the .perfetto-trace file "
        "(default: newest *.perfetto-trace under output/playwright/)",
    )
    parser.add_argument("--event", action="append", default=[], dest="events")
    parser.add_argument("--arg", action="append", default=[], dest="args")
    parser.add_argument("--category", default=None)
    parser.add_argument("--min-dur-ms", type=float, default=None)
    parser.add_argument("--max-dur-ms", type=float, default=None)
    parser.add_argument("--counter", action="append", default=[], dest="counters")
    options = parser.parse_args()

    if options.trace is None:
        trace_dir = os.path.join(REPO_ROOT, "output", "playwright")
        candidates = []
        if os.path.isdir(trace_dir):
            candidates = [
                os.path.join(trace_dir, name)
                for name in os.listdir(trace_dir)
                if name.endswith(".perfetto-trace")
            ]
        if not candidates:
            raise SystemExit(
                "No trace file given and none found under output/playwright/."
            )
        options.trace = max(candidates, key=os.path.getmtime)

    if not os.path.isfile(options.trace):
        raise SystemExit(f"Trace file not found: {options.trace}")

    events = options.events or ([] if options.counters else DEFAULT_EVENTS)
    arg_filters = parse_arg_filters(options.args)
    counter_filters = parse_arg_filters(options.counters)
    tp = find_trace_processor()

    for event in events:
        like = sql_string(f"%{event}%")

        count = run_query(
            tp,
            options.trace,
            f"SELECT COUNT(*) FROM slice WHERE name LIKE {like}",
        )
        if count < 1:
            raise SystemExit(f'No slices found matching "{event}".')

        if options.min_dur_ms is not None or options.max_dur_ms is not None:
            max_dur = run_query(
                tp,
                options.trace,
                "SELECT COALESCE(MAX(CAST(dur AS REAL) / 1000000.0), 0) "
                f"FROM slice WHERE name LIKE {like} AND dur > 0",
            )
            if options.min_dur_ms is not None and max_dur < options.min_dur_ms:
                raise SystemExit(
                    f'"{event}" max duration {max_dur:.2f} ms is below '
                    f"--min-dur-ms ({options.min_dur_ms})."
                )
            if options.max_dur_ms is not None and max_dur > options.max_dur_ms:
                raise SystemExit(
                    f'"{event}" max duration {max_dur:.2f} ms is above '
                    f"--max-dur-ms ({options.max_dur_ms})."
                )

        if options.category is not None:
            cat_count = run_query(
                tp,
                options.trace,
                "SELECT COUNT(*) FROM slice WHERE name LIKE "
                f"{like} AND category = {sql_string(options.category)}",
            )
            if cat_count < 1:
                raise SystemExit(
                    f'No slice matching "{event}" has category '
                    f'"{options.category}".'
                )

        for key, value in arg_filters:
            arg_count = run_query(
                tp,
                options.trace,
                "SELECT COUNT(*) FROM slice s "
                "JOIN args a ON a.arg_set_id = s.arg_set_id "
                f"WHERE s.name LIKE {like} "
                f"AND a.key IN ({sql_string(key)}, {sql_string('debug.' + key)}) "
                f"AND a.display_value = {sql_string(value)}",
            )
            if arg_count < 1:
                raise SystemExit(
                    f'No debug annotation {key}={value} on slices matching "{event}".'
                )

        print(f'verified slices matching "{event}"')

    for name, value in counter_filters:
        try:
            expected = float(value)
        except ValueError:
            raise SystemExit(f"Invalid --counter value: {value!r}. Expected numeric.")

        count = run_query(
            tp,
            options.trace,
            "SELECT COUNT(*) FROM counter c "
            "JOIN counter_track t ON c.track_id = t.id "
            f"WHERE t.name LIKE {sql_string('%' + name + '%')} "
            f"AND ABS(c.value - {expected}) < 0.000001",
        )
        if count < 1:
            raise SystemExit(
                f'No counter sample ~{value} on a counter track matching "{name}".'
            )
        print(f'verified counter "{name}" = {value}')

    print("all checks passed")


if __name__ == "__main__":
    main()
